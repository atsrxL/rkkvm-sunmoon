// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon/encoder.hpp"
#include <algorithm>
#include <charconv>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h>
namespace {
volatile sig_atomic_t stopping=0;void stop(int){stopping=1;}
struct ModeChanged:std::runtime_error{using std::runtime_error::runtime_error;};
uint32_t number(const std::string& s){uint32_t n{};auto r=std::from_chars(s.data(),s.data()+s.size(),n);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())throw std::runtime_error("invalid numeric argument");return n;}
void write_file(int fd,const std::vector<uint8_t>& data){size_t at=0;while(at<data.size()){auto n=write(fd,data.data()+at,data.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("output file write failed");at+=size_t(n);}}
}
int main(int argc,char** argv){
  using namespace rkmoon;using namespace std::chrono_literals;
  signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
  Config c;std::string device="/dev/video0",output,stats,pool_socket;uint32_t seconds=60,idr_at=30;int ipc=-1;
  bool probe=false,allow_copy=false,authorized=false,placeholder=false;
  try {
    for(int i=1;i<argc;++i){std::string k=argv[i];
      if(k=="--help"){std::cout<<"rkmoon-worker --device /dev/videoN --codec hevc|h264 --width 1920 --height 1080 --fps-x100 6000 --bitrate 20000000 --gop 60 --seconds 60 --output NEW_FILE --stats NEW_CSV --ack-capture-ownership [--allow-copy]\nEven 64..3840 x 64..2160, 1..120.10fps; throughput capped at 3840x2160x60.10. Legacy --allow-1440p90-experiment accepted.\nHardware-only capability probe: --probe (no HDMI capture; emits synthetic black into MPP)\nInternal: --ipc-fd FD; one AU per ACK, K/bitrate/stop control messages\n";return 0;}
      if(k=="--probe"){probe=true;continue;}if(k=="--allow-copy"){allow_copy=true;continue;}if(k=="--ack-capture-ownership"){authorized=true;continue;}
      if(k=="--no-signal-placeholder"){placeholder=true;continue;}
      if(k=="--allow-1440p90-experiment"){c.allow_1440p90_experiment=true;continue;}
      if(++i>=argc)throw std::runtime_error("missing option value");
      std::string v=argv[i];
      if(k=="--device")device=v;else if(k=="--output")output=v;else if(k=="--stats")stats=v;else if(k=="--capture-pool")pool_socket=v;
      else if(k=="--width")c.width=number(v);else if(k=="--height")c.height=number(v);else if(k=="--fps-x100")c.fps_x100=number(v);
      else if(k=="--bitrate")c.bitrate=number(v);else if(k=="--gop")c.gop=number(v);else if(k=="--seconds")seconds=number(v);
      else if(k=="--idr-at")idr_at=number(v);else if(k=="--ipc-fd")ipc=int(number(v));
      else if(k=="--codec"){if(v=="hevc")c.codec=Codec::hevc;else if(v=="h264")c.codec=Codec::h264;else throw std::runtime_error("unknown codec");}
      else throw std::runtime_error("unknown argument");
    }
    c.validate();if(seconds>86400||(!seconds&&ipc<0))throw std::runtime_error("invalid duration");
    if(ipc>=0){auto parent=getppid();if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent)throw std::runtime_error("worker parent lost");}
    if(probe){
      uint32_t caps=0;
      for(auto codec:{Codec::h264,Codec::hevc}) {
        try {Config pc=c;pc.codec=codec;Layout l{pc.width,pc.height,pc.width,pc.width*pc.height*3/2,Pixels::nv12,false};
          Encoder enc;enc.open(pc,l,nullptr,true);Capture::Frame f{};f.dequeue_us=now_us();auto au=enc.encode(f,nullptr,true);validate_au(au,true);caps|=codec==Codec::h264?1:2;
        }catch(const std::exception& e){std::cerr<<"hardware probe failed: "<<e.what()<<'\n';}
      }
      if(ipc>=0){Message m;m.h.kind=Kind::caps;m.h.extra=caps;send(ipc,m,2s);}
      std::cout<<"{\"hardware_encoder_probe\":true,\"synthetic_input\":true,\"codec_mask\":"<<caps<<"}\n";
      return caps&1?0:1;
    }
    if(!authorized)throw std::runtime_error("capture ownership not authorized; no device opened");
    // Shared by the placeholder and capture loops: one AU in flight, controls honored while waiting.
    auto await_ack=[&](uint64_t seq,bool& force,const std::function<void(uint32_t)>& bitrate)->bool{
      auto deadline=now_us()+2000000;
      while(!stopping){
        auto now=now_us();if(now>=deadline)throw std::runtime_error("AU credit timeout");
        auto ctl=receive(ipc,std::chrono::milliseconds((deadline-now+999)/1000));
        switch(ctl.h.kind){
          case Kind::ack:if(ctl.h.seq!=seq)throw std::runtime_error("ACK sequence mismatch");return true;
          case Kind::idr:force=true;break;
          case Kind::bitrate:bitrate(ctl.h.extra);break;
          case Kind::stop:return false;
          default:throw std::runtime_error("unexpected worker control");
        }
      }
      return false;
    };
    if(placeholder){
      // ADR-011: no HDMI source. The session still opens so keyboard/mouse can wake the target.
      // The HDMI device is never opened; MPP hardware-encodes synthetic black at a low cadence.
      // The client restarts the session in the real mode as soon as the server reports a signal.
      if(ipc<0)throw std::runtime_error("placeholder mode is session-only");
      Layout l{c.width,c.height,c.width,c.width*c.height*3/2,Pixels::nv12,false};
      Encoder enc;enc.open(c,l,nullptr,true);
      Message m;m.h.kind=Kind::ready;m.h.width=c.width;m.h.height=c.height;m.h.codec=c.codec;m.h.extra=c.fps_x100;send(ipc,m,2s);
      std::cerr<<"{\"kind\":\"placeholder\",\"reason\":\"no HDMI signal\"}\n";
      constexpr uint64_t period_us=100000; // 10 black frames/s keep the stream and input alive.
      bool force=true;auto next=now_us();
      auto bitrate=[&](uint32_t bps){enc.bitrate(bps);force=true;};
      while(!stopping){
        for(auto now=now_us();now<next&&!stopping;now=now_us()){
          if(!readable(ipc,std::chrono::milliseconds((next-now+999)/1000)))continue;
          auto ctl=receive(ipc,100ms);
          if(ctl.h.kind==Kind::stop)return 0;
          if(ctl.h.kind==Kind::idr)force=true;
          else if(ctl.h.kind==Kind::bitrate)bitrate(ctl.h.extra);
          else throw std::runtime_error("unexpected control before frame");
        }
        next=std::max(next+period_us,now_us());
        Capture::Frame black{};black.dequeue_us=now_us();
        auto frame=enc.encode(black,nullptr,force);force=false;
        send(ipc,frame,300ms);
        if(!await_ack(frame.h.seq,force,bitrate))return 0;
      }
      return 0;
    }
    if(ipc<0&&output.empty())throw std::runtime_error("standalone mode requires --output");
    Fd file;if(!output.empty()){file.reset(open(output.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));if(file.get()<0)throw std::runtime_error("output already exists or cannot be created");}
    Fd statfd;if(!stats.empty()){statfd.reset(open(stats.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));if(statfd.get()<0)throw std::runtime_error("stats already exist/cannot create");}
    auto emit=[&](const std::string& s){if(statfd.get()>=0)write_file(statfd.get(),std::vector<uint8_t>(s.begin(),s.end()));};
    emit("seq,driver_seq,driver_timestamp_us,driver_timestamp_flags,dequeue_us,submit_us,done_us,bytes,idr,raw_skipped,dmabuf\n");
    // Destruction order is intentional: enc dies before cap on all exits, including exceptions.
    std::unique_ptr<Capture> capture;std::unique_ptr<Encoder> encoder;
    // ADR-013: boot-time contiguous buffers; empty when the pool service is absent.
    auto pool=borrow_capture_pool(pool_socket);
    if(!pool_socket.empty()&&pool.empty())std::cerr<<"{\"kind\":\"capture_pool_unavailable\"}\n";
    struct PoolGuard{std::vector<PoolBuffer>& p;~PoolGuard(){return_capture_pool(p);}} pool_guard{pool};
    auto open_pipeline=[&]{
      auto next=std::make_unique<Capture>(device);next->describe();
      // A different source mode is not a transient fault: the client must renegotiate.
      try{next->validate(c);}catch(const std::exception& e){throw ModeChanged(e.what());}
      next->start(direct_layout(next->layout),&pool);
      auto e=std::make_unique<Encoder>();e->open(c,next->layout,&next->buffers(),allow_copy);
      encoder.reset();capture=std::move(next);encoder=std::move(e);
    };
    open_pipeline();
    // Transient HDMI capture faults (frame timeout, DQBUF errors) are recovered inside the worker:
    // tear down encoder then capture, reopen with the SAME negotiated mode, continue the AU sequence
    // and force an IDR. A changed/lost source is still fatal after the bounded recovery window.
    constexpr uint64_t recovery_window_us=5000000;uint64_t recovering_since=0;uint32_t recoveries=0;
    auto recover=[&](const std::exception& e){
      auto now=now_us();if(!recovering_since)recovering_since=now;
      if(now-recovering_since>recovery_window_us)throw std::runtime_error(std::string("capture recovery window exceeded: ")+e.what());
      std::cerr<<"{\"kind\":\"capture_recover\",\"attempt\":"<<++recoveries<<",\"reason\":\""<<e.what()<<"\"}\n";
      auto last=encoder?encoder->sequence():0;
      encoder.reset();capture.reset();
      std::this_thread::sleep_for(100ms);
      if(stopping)return;
      try{open_pipeline();encoder->continue_sequence(last);}
      catch(const ModeChanged&){throw;}
      catch(const std::exception& again){std::cerr<<"{\"kind\":\"capture_recover_failed\",\"reason\":\""<<again.what()<<"\"}\n";encoder.reset();capture.reset();}
    };
    if(ipc>=0){Message m;m.h.kind=Kind::ready;m.h.width=c.width;m.h.height=c.height;m.h.codec=c.codec;m.h.extra=c.fps_x100;send(ipc,m,2s);}
    auto start=now_us(),last_check=start;bool force=true,sent_test_idr=false;uint64_t count=0;uint32_t pending_bitrate=0;
    auto set_bitrate=[&](uint32_t bps){pending_bitrate=bps;if(encoder){encoder->bitrate(bps);pending_bitrate=0;}force=true;};
    while(!stopping&&(ipc>=0||now_us()-start<uint64_t(seconds)*1000000)){
      if(ipc>=0){ // Drain queued controls BEFORE picking the next raw frame.
        while(readable(ipc,0ms)){
          auto ctl=receive(ipc,100ms);
          if(ctl.h.kind==Kind::stop)return 0;
          if(ctl.h.kind==Kind::idr)force=true;
          else if(ctl.h.kind==Kind::bitrate)set_bitrate(ctl.h.extra);
          else throw std::runtime_error("unexpected control before frame");
        }
      }
      if(!encoder){ // Previous reopen failed; keep retrying inside the bounded window.
        recover(std::runtime_error("capture reopen pending"));
        if(encoder){force=true;if(pending_bitrate)set_bitrate(pending_bitrate);}
        continue;
      }
      auto& cap=*capture;auto& enc=*encoder;
      if(!sent_test_idr&&idr_at&&now_us()-start>=uint64_t(idr_at)*1000000){force=true;sent_test_idr=true;}
      Capture::Frame raw{};
      try{raw=cap.latest(1000ms);}
      catch(const std::exception& e){
        recover(e);
        if(encoder){force=true;if(pending_bitrate)set_bitrate(pending_bitrate);}
        continue;
      }
      auto frame=enc.encode(raw,&cap.buffer(raw.index),force);force=false;
      recovering_since=0;
      cap.release(raw.index); // ONLY after full AU completion, never on encoder error.
      ++count;if(file.get()>=0)write_file(file.get(),frame.bytes);
      auto& h=frame.h;emit(std::to_string(h.seq)+","+std::to_string(raw.sequence)+","+std::to_string(raw.driver_us)+","+std::to_string(raw.flags&V4L2_BUF_FLAG_TIMESTAMP_MASK)+","+std::to_string(h.dequeue_us)+","+std::to_string(h.submit_us)+","+std::to_string(h.done_us)+","+std::to_string(h.size)+","+std::to_string(bool(h.flags&flag_idr))+","+std::to_string(cap.raw_skipped)+","+std::to_string(enc.direct())+"\n");
      if(ipc>=0){
        send(ipc,frame,300ms);
        if(!await_ack(h.seq,force,set_bitrate))return 0;
      }
      if(now_us()-last_check>=1000000){
        try{cap.unchanged();}
        catch(const std::exception& e){last_check=now_us();recover(e);if(encoder){force=true;if(pending_bitrate)set_bitrate(pending_bitrate);}continue;}
        last_check=now_us();
        std::cerr<<"{\"kind\":\"stats\",\"encoded\":"<<count<<",\"raw_skipped\":"<<cap.raw_skipped<<",\"elapsed_us\":"<<last_check-start<<",\"dequeue_to_au_us\":"<<h.done_us-h.dequeue_us<<",\"au_bytes\":"<<h.size<<"}\n";
      }
    }
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"rkmoon-worker: "<<e.what()<<'\n';
    // Tell the server why, so its session log names the cause instead of "IPC peer closed".
    if(ipc>=0){try{std::string why=e.what();if(why.size()>1000)why.resize(1000);
      Message m;m.h.kind=Kind::error;m.bytes.assign(why.begin(),why.end());m.h.size=uint32_t(m.bytes.size());send(ipc,m,200ms);}catch(...){}}
    return 75;
  }
}
