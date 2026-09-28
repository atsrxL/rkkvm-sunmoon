// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon_bridge.hpp"
#include "rkmoon_display.hpp"
#include "rkmoon/core.hpp"
#include "rkmoon/hid_client.hpp"
#include "src/globals.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/utility.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unistd.h>
namespace rkmoon_sunshine {
using namespace std::chrono_literals;
namespace {
std::mutex video_owner,input_mutex;
std::shared_ptr<rkmoon::HidClient> input;
std::string env(const char* key,const char* fallback=""){const char* v=getenv(key);return v?v:fallback;}
bool yes(const char* key){return env(key)=="1";}
void forward(rkmoon::HidEvent e){std::shared_ptr<rkmoon::HidClient> p;{std::lock_guard lock(input_mutex);p=input;}if(p)p->submit(e);}
rkmoon::Config translate(const video::config_t& c){
  if(c.rkmoon_absolute_mouse&&!yes("RKMOON_ALLOW_ABSOLUTE_MOUSE"))throw std::runtime_error("absolute mouse not authorized");
  rkmoon::Config out;
  if(c.videoFormat<0||c.videoFormat>1||c.dynamicRange!=0||c.chromaSamplingType!=0||c.enableIntraRefresh)throw std::runtime_error("RKMoon only supports SDR 8-bit 4:2:0 H264/HEVC, IDR recovery");
  if(c.bitrate<1000||c.bitrate>35000)throw std::runtime_error("RKMoon negotiated video bitrate must be 1..35 Mbps");
  out.width=uint32_t(c.width);out.height=uint32_t(c.height);
  if(c.framerateX100<=0 && (c.framerate<1||c.framerate>120))throw std::runtime_error("invalid integer framerate");
  out.fps_x100=uint32_t(c.framerateX100>0?c.framerateX100:c.framerate*100);
  out.fps_x100=effective_capture_fps(out.width,out.height,out.fps_x100,current_display());
  out.bitrate=uint32_t(c.bitrate)*1000;out.codec=c.videoFormat==1?rkmoon::Codec::hevc:rkmoon::Codec::h264;
  if(!yes("RKMOON_ALLOW_HIGH_RES")&&(out.width!=1920||out.height!=1080))throw std::runtime_error("RKMoon first-stage mode is 1920x1080; higher modes require explicit acceptance gate");
  out.allow_1440p90_experiment=yes("RKMOON_ALLOW_1440P90_EXPERIMENT")&&yes("RKMOON_ALLOW_HIGH_RES");
  out.validate();return out;
}
std::vector<std::string> args(const rkmoon::Config& c,bool placeholder){
  std::vector<std::string> a{"--device",env("RKMOON_VIDEO_DEVICE","/dev/video0"),"--codec",c.codec==rkmoon::Codec::hevc?"hevc":"h264",
    "--width",std::to_string(c.width),"--height",std::to_string(c.height),"--fps-x100",std::to_string(c.fps_x100),"--bitrate",std::to_string(c.bitrate),"--gop",std::to_string(std::clamp((c.fps_x100+50)/100,1U,120U)),"--ack-capture-ownership"};
  if(c.allow_1440p90_experiment&&c.fps_x100>=8900) a.push_back("--allow-1440p90-experiment");
  if(yes("RKMOON_ALLOW_COPY")) a.push_back("--allow-copy");
  if(!env("RKMOON_CAPTURE_POOL").empty()){a.push_back("--capture-pool");a.push_back(env("RKMOON_CAPTURE_POOL"));}
  if(placeholder) a.push_back("--no-signal-placeholder");
  return a;
}
// The worker reports its failure reason as an error message before exiting.
void check_worker_error(const rkmoon::Message& m){
  if(m.h.kind==rkmoon::Kind::error)throw std::runtime_error("worker: "+std::string(m.bytes.begin(),m.bytes.end()));
}
void release_input(){std::shared_ptr<rkmoon::HidClient> old;{std::lock_guard lock(input_mutex);old=std::move(input);}old.reset();}
// Input is optional for a session: video keeps streaming while the HID lease is refused or
// lost, and a background thread re-acquires it every 2s. The HID bridge still refuses new
// leases while a failed release is pending, so a stuck key can never be re-armed.
void maintain_input(std::stop_token stop,std::string path,bool absolute){
  std::mutex m;std::condition_variable_any cv;bool warned=false;
  while(!stop.stop_requested()){
    bool need;{std::lock_guard lock(input_mutex);need=!input||!input->healthy();}
    if(need){
      release_input();
      try{
        auto fresh=std::make_shared<rkmoon::HidClient>(path,absolute);
        if(stop.stop_requested())return;
        {std::lock_guard lock(input_mutex);input=std::move(fresh);}
        BOOST_LOG(info)<<"RKMoon input lease acquired";warned=false;
      }catch(const std::exception& e){
        if(!warned){BOOST_LOG(warning)<<"RKMoon input unavailable, video continues without input: "<<e.what();warned=true;}
      }
    }
    std::unique_lock lock(m);cv.wait_for(lock,stop,std::chrono::seconds(need?2:1),[]{return false;});
  }
}
}
bool enabled() noexcept{return true;}
bool request_supported(const video::config_t& c) noexcept{try{translate(c);return yes("RKMOON_CAPTURE_AUTHORIZED");}catch(...){return false;}}
int probe(){
  video::active_hevc_mode=1;video::active_av1_mode=1;
  video::last_encoder_probe_supported_ref_frames_invalidation=false;
  video::last_encoder_probe_supported_yuv444_for_codec.fill(false);
  try {
    if(geteuid()==0)throw std::runtime_error("run dedicated Sunshine unprivileged with device ACLs; do not add file capabilities");
    rkmoon::Child worker(env("RKMOON_WORKER"),{"--probe"});
    auto response=rkmoon::receive(worker.fd(),3000ms);
    if(response.h.kind!=rkmoon::Kind::caps||!(response.h.extra&1)||(response.h.extra&~3U))throw std::runtime_error("hardware H264 probe failed/invalid capability report");
    video::active_hevc_mode=(response.h.extra&2)?2:1;
    BOOST_LOG(info)<<"RKMOON_DEDICATED_BUILD_v1 RKMoon hardware encoder probe succeeded; HEVC="<<(video::active_hevc_mode==2)<<"; this is not HDMI/Moonlight acceptance";
    return 0;
  }catch(const std::exception& e){BOOST_LOG(error)<<"RKMoon probe failed, NO software/desktop fallback: "<<e.what();return -1;}
}
void capture(safe::mail_t mail,video::config_t config,void* channel_data){
  auto shutdown_event=mail->event<bool>(mail::shutdown);
  std::unique_lock owner(video_owner,std::try_to_lock);
  if(!owner.owns_lock()){BOOST_LOG(error)<<"RKMoon exclusive capture busy";shutdown_event->raise(true);return;}
  // An explicit scope ensures input EOF/release happens before this owner can be replaced.
  auto cleanup=util::fail_guard([&]{release_input();shutdown_event->raise(true);});
  try {
    if(!yes("RKMOON_CAPTURE_AUTHORIZED"))throw std::runtime_error("capture ownership has not been granted");
    auto c=translate(config);
    // ADR-011: with no HDMI source the session still opens (hardware-encoded black) so the
    // keyboard/mouse can wake the target. The client restarts in the real mode once it appears.
    const bool placeholder=std::string(current_display().status)=="no_signal";
    if(placeholder)BOOST_LOG(info)<<"RKMoon no HDMI signal: starting input session with black placeholder video";
    rkmoon::Child worker(env("RKMOON_WORKER"),args(c,placeholder));
    auto ready=rkmoon::receive(worker.fd(),3000ms);
    check_worker_error(ready);
    if(ready.h.kind!=rkmoon::Kind::ready||ready.h.width!=c.width||ready.h.height!=c.height||ready.h.codec!=c.codec||ready.h.extra!=c.fps_x100)throw std::runtime_error("capture negotiation failed");
    // HDMI is the entire input viewport. No T6 desktop layout or logical scaling.
    mail->event<::input::touch_port_t>(mail::touch_port)->raise(::input::touch_port_t{
      {0,0,int(c.width),int(c.height),0,0},int(c.width),int(c.height),0,0,1,1,0,0});
    mail->event<video::hdr_info_t>(mail::hdr)->raise(std::make_unique<video::hdr_info_raw_t>(false));
    // Declared after 'cleanup': joined (and any in-flight acquisition finished) before release_input().
    std::jthread input_keeper;
    if(!env("RKMOON_HID_SOCKET").empty())
      input_keeper=std::jthread(maintain_input,env("RKMOON_HID_SOCKET"),bool(config.rkmoon_absolute_mouse));
    auto idr=mail->event<bool>(mail::idr);
    auto queue=mail::man->queue<video::packet_t>(mail::video_packets);
    rkmoon::SequenceGate gate;
    uint64_t last_progress=rkmoon::now_us(),last_log=last_progress;size_t queue_high=0;
    // After a sender overload, drop dependent frames until the worker delivers a fresh IDR,
    // like upstream Sunshine's loss recovery. Reference frames are never sent out of order.
    bool await_idr=false;uint64_t dropped=0;
    auto request_idr=[&]{rkmoon::Message ctl;ctl.h.kind=rkmoon::Kind::idr;rkmoon::send(worker.fd(),ctl,100ms);};
    while(!shutdown_event->peek()) {
      if(idr->peek()) {idr->pop();request_idr();}
      if(!rkmoon::readable(worker.fd(),20ms)) {
        // Worker recovers transient capture faults for up to 5s; allow that window plus margin.
        if(rkmoon::now_us()-last_progress>7000000)throw std::runtime_error("HDMI/worker stalled");
        continue;
      }
      auto frame=rkmoon::receive(worker.fd(),300ms);check_worker_error(frame);gate.accept(frame);
      if(frame.h.width!=c.width||frame.h.height!=c.height||frame.h.codec!=c.codec)throw std::runtime_error("frame format epoch mismatch");
      auto now=rkmoon::now_us();
      if(frame.h.done_us>now+1000||frame.h.dequeue_us>now)throw std::runtime_error("frame timestamps from the future");
      auto seq=frame.h.seq,dq=frame.h.dequeue_us;
      const bool is_idr=frame.h.flags&rkmoon::flag_idr;
      if(await_idr&&is_idr)await_idr=false;
      bool deliver=!await_idr&&now-dq<=200000;
      if(deliver){
        video::packet_t packet=std::make_unique<video::packet_raw_generic>(std::move(frame.bytes),int64_t(seq),is_idr);
        packet->channel_data=channel_data;
        packet->frame_timestamp=std::chrono::steady_clock::now()-std::chrono::microseconds(now-dq);
        // Wait briefly for sender room; stale frames are dropped (with IDR recovery) instead of queued.
        while(!queue->rkmoon_try_raise(2,std::move(packet))){
          if(shutdown_event->peek()||!queue->rkmoon_running())throw std::runtime_error("video sender stopped");
          if(rkmoon::now_us()-dq>50000){deliver=false;break;}
          std::this_thread::sleep_for(1ms);
        }
      }
      if(!deliver){
        ++dropped;
        if(!await_idr){await_idr=true;request_idr();}
      }
      queue_high=std::max(queue_high,queue->rkmoon_size());
      rkmoon::Message ack;ack.h.kind=rkmoon::Kind::ack;ack.h.seq=seq;rkmoon::send(worker.fd(),ack,100ms);
      last_progress=rkmoon::now_us();
      if(last_progress-last_log>=1000000){
        BOOST_LOG(info)<<"RKMoon frames="<<seq<<" sender_queue_highwater="<<queue_high<<" dropped_for_idr="<<dropped<<" dequeue_to_enqueue_us="<<last_progress-dq;
        last_log=last_progress;
      }
    }
  }catch(const std::exception& e){BOOST_LOG(error)<<"RKMoon session ended: "<<e.what();}
}
void key(uint16_t vk,bool release,uint8_t flags){(void)flags;forward({rkmoon::HidEvent::key,int(vk),0,!release});}
void relative(int x,int y){forward({rkmoon::HidEvent::move,x,y,false});}
void absolute(const platf::touch_port_t& port,float x,float y){
  if(port.width<=0||port.height<=0||!std::isfinite(x)||!std::isfinite(y))return;
  forward({rkmoon::HidEvent::absolute,rkmoon::absolute_coordinate(x-port.offset_x,port.width),rkmoon::absolute_coordinate(y-port.offset_y,port.height),false});
}
void button(int number,bool release){forward({rkmoon::HidEvent::button,number,0,!release});}
void scroll(int x,int y){forward({rkmoon::HidEvent::wheel,x,y,false});}
} // namespace rkmoon_sunshine
