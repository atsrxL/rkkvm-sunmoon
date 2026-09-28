// SPDX-License-Identifier: GPL-3.0-or-later
// Layout/import approach informed by nanopc-t6-kvm native/capture.c (GPL-3.0-or-later).
// Unlike that reference, this implementation NEVER changes source timings or format.
#include "rkmoon/capture.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/dma-buf.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <string>
namespace rkmoon {
std::vector<PoolBuffer> borrow_capture_pool(const std::string& path) {
  std::vector<PoolBuffer> out;
  if(path.empty()||path.size()>=sizeof(sockaddr_un::sun_path)) return out;
  Fd s(socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0));if(s.get()<0) return out;
  sockaddr_un a{};a.sun_family=AF_UNIX;std::memcpy(a.sun_path,path.c_str(),path.size());
  if(connect(s.get(),reinterpret_cast<sockaddr*>(&a),sizeof(a))<0) return out;
  timeval tv{1,0};setsockopt(s.get(),SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  char text[256]{};iovec io{text,sizeof(text)-1};
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int)*4)]{};
  msghdr m{};m.msg_iov=&io;m.msg_iovlen=1;m.msg_control=control;m.msg_controllen=sizeof(control);
  ssize_t n;do{n=recvmsg(s.get(),&m,MSG_CMSG_CLOEXEC);}while(n<0&&errno==EINTR);
  std::vector<int> fds;
  for(auto* c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c))
    if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS)
      for(size_t i=0;i<(c->cmsg_len-CMSG_LEN(0))/sizeof(int);++i){int fd;std::memcpy(&fd,CMSG_DATA(c)+i*sizeof(int),sizeof(int));fds.push_back(fd);}
  // Header: {"version": 1, "count": N, "size": BYTES}
  std::string h(text,n>0?size_t(n):0);auto sz=h.find("\"size\":");
  size_t size=sz==std::string::npos?0:std::strtoull(h.c_str()+sz+7,nullptr,10);
  if(n<=0||(m.msg_flags&MSG_CTRUNC)||h.find("\"version\": 1")==std::string::npos||size<(1u<<20)||fds.size()<2){
    for(int fd:fds) close(fd);return out;
  }
  for(int fd:fds) out.push_back({fd,size});
  return out;
}
void return_capture_pool(std::vector<PoolBuffer>& pool){for(auto& p:pool) if(p.fd>=0) close(p.fd);pool.clear();}
namespace {
int ctl(int fd,unsigned long op,void* p) {int r;do {r=ioctl(fd,op,p);}while(r<0&&errno==EINTR);return r;}
// Imported dma-bufs are not cache-managed by vb2 on DQBUF/QBUF; bracket CPU reads explicitly.
void cpu_access(int fd,uint64_t flags){dma_buf_sync s{};s.flags=flags|DMA_BUF_SYNC_READ;ctl(fd,DMA_BUF_IOCTL_SYNC,&s);}
void check(int r,const char* msg) {if(r<0)throw std::runtime_error(std::string(msg)+": "+std::strerror(errno));}
// Same errno set the server advertises as RKMoonDisplayStatus=no_signal.
void query_timings(int fd,v4l2_dv_timings& t,const char* msg) {
  if(ctl(fd,VIDIOC_QUERY_DV_TIMINGS,&t)<0) {
    if(errno==ENOLINK||errno==ENOLCK||errno==ENODATA) throw NoSignal(std::string("no HDMI signal: ")+std::strerror(errno));
    check(-1,msg);
  }
  if(!t.bt.width||!t.bt.height) throw NoSignal("no HDMI signal: empty timings");
}
double cadence(const v4l2_dv_timings& t) {
  uint64_t w=uint64_t(t.bt.width)+t.bt.hfrontporch+t.bt.hsync+t.bt.hbackporch;
  uint64_t h=uint64_t(t.bt.height)+t.bt.vfrontporch+t.bt.vsync+t.bt.vbackporch;
  return w&&h?double(t.bt.pixelclock)/double(w*h):0;
}
}
Capture::Capture(const std::string& device):fd_(open(device.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC)) {
  check(fd_.get(),"open V4L2"); struct stat st{}; check(fstat(fd_.get(),&st),"fstat V4L2");
  if(!S_ISCHR(st.st_mode)) throw std::runtime_error("V4L2 path is not a character device");
  v4l2_capability cap{};check(ctl(fd_.get(),VIDIOC_QUERYCAP,&cap),"QUERYCAP");
  auto c=(cap.capabilities&V4L2_CAP_DEVICE_CAPS)?cap.device_caps:cap.capabilities;
  if(!(c&V4L2_CAP_VIDEO_CAPTURE_MPLANE)||!(c&V4L2_CAP_STREAMING)) throw std::runtime_error("requires streaming V4L2 MPLANE capture");
  query();
}
void Capture::query() {
  query_timings(fd_.get(),timings_,"QUERY_DV_TIMINGS");
  v4l2_format f{};f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  check(ctl(fd_.get(),VIDIOC_G_FMT,&f),"G_FMT");format_=f.fmt.pix_mp;
  signal_fps=cadence(timings_);
  layout.width=format_.width; layout.height=format_.height;
  layout.stride=format_.plane_fmt[0].bytesperline; layout.sizeimage=format_.plane_fmt[0].sizeimage;
  if(format_.pixelformat==V4L2_PIX_FMT_NV12) layout.pixels=Pixels::nv12;
  else if(format_.pixelformat==V4L2_PIX_FMT_BGR24) layout.pixels=Pixels::bgr24;
  else throw std::runtime_error("v1 only accepts one-plane native NV12/BGR24; do not auto S_FMT");
  layout.transfer=(format_.xfer_func==V4L2_XFER_FUNC_SRGB || (format_.xfer_func==V4L2_XFER_FUNC_DEFAULT&&format_.colorspace==V4L2_COLORSPACE_SRGB))?13:1;
  layout.full_range=layout.pixels==Pixels::nv12&&format_.quantization==V4L2_QUANTIZATION_FULL_RANGE;
  layout.rgb_limited=layout.pixels==Pixels::bgr24&&format_.quantization==V4L2_QUANTIZATION_LIM_RANGE;
}
void Capture::describe() const {
  std::cerr<<"{\"kind\":\"capture_format\",\"width\":"<<layout.width<<",\"height\":"<<layout.height
    <<",\"fourcc\":"<<format_.pixelformat<<",\"planes\":"<<unsigned(format_.num_planes)<<",\"stride\":"<<layout.stride
    <<",\"sizeimage\":"<<layout.sizeimage<<",\"fps\":"<<signal_fps<<",\"colorspace\":"<<format_.colorspace
    <<",\"ycbcr_enc\":"<<unsigned(format_.ycbcr_enc)<<",\"quantization\":"<<unsigned(format_.quantization)
    <<",\"xfer_func\":"<<unsigned(format_.xfer_func)<<"}\n";
}
void Capture::validate(const Config& c) const {
  c.validate();
  if(timings_.type!=V4L2_DV_BT_656_1120||timings_.bt.interlaced||format_.field!=V4L2_FIELD_NONE||format_.num_planes!=1) throw std::runtime_error("unsupported interlaced/multiplane layout");
  if(c.width!=layout.width||c.height!=layout.height||timings_.bt.width!=c.width||timings_.bt.height!=c.height) throw std::runtime_error("client/source dimensions do not match; no scaling or modeset");
  if(!std::isfinite(signal_fps)||std::abs(signal_fps-double(c.fps_x100)/100)>0.15) throw std::runtime_error("source/client cadence mismatch");
  if(format_.colorspace!=V4L2_COLORSPACE_REC709&&format_.colorspace!=V4L2_COLORSPACE_SRGB) throw std::runtime_error("v1 requires explicitly identified BT709/sRGB SDR input");
  if(format_.xfer_func!=V4L2_XFER_FUNC_DEFAULT&&format_.xfer_func!=V4L2_XFER_FUNC_709&&format_.xfer_func!=V4L2_XFER_FUNC_SRGB) throw std::runtime_error("HDR/unknown transfer rejected");
  if(format_.quantization>V4L2_QUANTIZATION_LIM_RANGE) throw std::runtime_error("unknown quantization range");
  auto enc=format_.ycbcr_enc;
  if(enc==V4L2_YCBCR_ENC_DEFAULT) enc=V4L2_MAP_YCBCR_ENC_DEFAULT(format_.colorspace);
  if(layout.pixels==Pixels::nv12 && enc!=V4L2_YCBCR_ENC_709) throw std::runtime_error("v1 NV12 requires BT709 matrix");
  // No hidden chroma offset/padding heuristic. Only the explicitly packed layout is accepted.
  uint64_t bytes=uint64_t(layout.stride)*layout.height*(layout.pixels==Pixels::nv12?3:2)/2;
  if(layout.stride<uint64_t(layout.width)*(layout.pixels==Pixels::bgr24?3:1)||bytes!=layout.sizeimage) throw std::runtime_error("ambiguous packed plane layout/sizeimage");
}
void Capture::start(bool export_dma,const std::vector<PoolBuffer>* pool) {
  if(streaming_||!buffers_.empty()) throw std::runtime_error("capture already started");
  // Advisory lock protects cooperating instances, NOT older services. Operator must grant ownership.
  check(flock(fd_.get(),LOCK_EX|LOCK_NB),"capture advisory lock");
  v4l2_event_subscription sub{};sub.type=V4L2_EVENT_SOURCE_CHANGE;
  if(ctl(fd_.get(),VIDIOC_SUBSCRIBE_EVENT,&sub)<0&&errno!=EINVAL) check(-1,"SUBSCRIBE_EVENT");
  if(pool&&!pool->empty()) {
    try { start_pool(*pool); }
    catch(const std::exception& e) {
      std::cerr<<"{\"kind\":\"capture_pool_unused\",\"reason\":\""<<e.what()<<"\"}\n";
      free_buffers();
    }
    if(streaming_) return;
  }
  start_mmap(export_dma);
}
void Capture::free_buffers() {
  if(streaming_){auto type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;ctl(fd_.get(),VIDIOC_STREAMOFF,&type);streaming_=false;}
  for(auto& b:buffers_) if(b.data) munmap(b.data,b.size);
  buffers_.clear();
  v4l2_requestbuffers req{};req.count=0;req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;req.memory=memory_;
  ctl(fd_.get(),VIDIOC_REQBUFS,&req);
  memory_=V4L2_MEMORY_MMAP;
}
void Capture::start_pool(const std::vector<PoolBuffer>& pool) {
  // ADR-013: buffers are CMA-contiguous dma-bufs allocated at boot, so a long-running host
  // never needs a fresh multi-megabyte contiguous allocation when a session starts.
  size_t usable=0;
  for(const auto& p:pool) if(p.fd>=0&&p.size>=layout.sizeimage) ++usable; else break;
  if(usable<2) throw std::runtime_error("pool buffers smaller than this mode");
  memory_=V4L2_MEMORY_DMABUF;
  v4l2_requestbuffers req{};req.count=uint32_t(std::min<size_t>(usable,4));req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;req.memory=memory_;
  check(ctl(fd_.get(),VIDIOC_REQBUFS,&req),"REQBUFS(DMABUF)");
  if(req.count<2||req.count>std::min<size_t>(usable,4)) throw std::runtime_error("unexpected pooled buffer count");
  buffers_.resize(req.count);
  for(uint32_t i=0;i<req.count;++i) {
    auto& dst=buffers_[i];dst.size=pool[i].size;
    dst.dma.reset(fcntl(pool[i].fd,F_DUPFD_CLOEXEC,3));check(dst.dma.get(),"dup pool buffer");
    dst.data=mmap(nullptr,dst.size,PROT_READ,MAP_SHARED,dst.dma.get(),0);
    if(dst.data==MAP_FAILED){dst.data=nullptr;check(-1,"mmap pool buffer");}
    dst.dequeued=true;release(i);
  }
  auto type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  check(ctl(fd_.get(),VIDIOC_STREAMON,&type),"STREAMON(DMABUF)");streaming_=true;
  std::cerr<<"{\"kind\":\"capture_pool\",\"buffers\":"<<req.count<<",\"buffer_bytes\":"<<pool[0].size<<"}\n";
}
void Capture::start_mmap(bool export_dma) {
  memory_=V4L2_MEMORY_MMAP;
  v4l2_requestbuffers req{};req.count=4;req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;req.memory=V4L2_MEMORY_MMAP;
  check(ctl(fd_.get(),VIDIOC_REQBUFS,&req),"REQBUFS (per-session CMA allocation; boot-time pool unavailable)");
  if(req.count<2||req.count>4) throw std::runtime_error("unexpected capture buffer count");
  buffers_.resize(req.count);
  for(uint32_t i=0;i<req.count;++i) {
    v4l2_plane plane{};v4l2_buffer b{};b.type=req.type;b.memory=req.memory;b.index=i;b.length=1;b.m.planes=&plane;
    check(ctl(fd_.get(),VIDIOC_QUERYBUF,&b),"QUERYBUF");
    auto& dst=buffers_[i];dst.size=plane.length;
    dst.data=mmap(nullptr,dst.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd_.get(),plane.m.mem_offset);
    if(dst.data==MAP_FAILED){dst.data=nullptr;check(-1,"MMAP");}
    if(dst.size<layout.sizeimage) throw std::runtime_error("capture buffer shorter than sizeimage");
    if(export_dma) {
      v4l2_exportbuffer exp{};exp.type=req.type;exp.index=i;exp.plane=0;exp.flags=O_CLOEXEC;
      if(ctl(fd_.get(),VIDIOC_EXPBUF,&exp)==0) dst.dma.reset(exp.fd);
      // An explicit allow-copy gate lives in Encoder; this does not silently change hardware encoding.
    }
    dst.dequeued=true;release(i);
  }
  auto type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  check(ctl(fd_.get(),VIDIOC_STREAMON,&type),"STREAMON");streaming_=true;
}
void Capture::release(uint32_t i) {
  auto& buf=buffers_.at(i);
  if(!buf.dequeued) throw std::runtime_error("double QBUF");
  if(memory_==V4L2_MEMORY_DMABUF&&buf.cpu){cpu_access(buf.dma.get(),DMA_BUF_SYNC_END);buf.cpu=false;}
  v4l2_plane p{};v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b.memory=memory_;b.index=i;b.length=1;b.m.planes=&p;
  if(memory_==V4L2_MEMORY_DMABUF){p.m.fd=buf.dma.get();p.length=uint32_t(buf.size);}
  check(ctl(fd_.get(),VIDIOC_QBUF,&b),"QBUF");buf.dequeued=false;
}
Capture::Frame Capture::latest(std::chrono::milliseconds timeout) {
  pollfd p{fd_.get(),POLLIN|POLLPRI,0};int r;
  do{r=poll(&p,1,int(timeout.count()));}while(r<0&&errno==EINTR);
  check(r,"capture poll"); if(!r) throw std::runtime_error("HDMI frame timeout");
  if(p.revents&(POLLERR|POLLHUP|POLLNVAL|POLLPRI)) throw std::runtime_error("HDMI change/error; reconnect required");
  Frame latest{};bool have=false;
  // Bound drain to the buffer count; QBUF may allow new frames while draining.
  for(size_t attempt=0;attempt<buffers_.size();++attempt) {
    v4l2_plane plane{};v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b.memory=memory_;b.length=1;b.m.planes=&plane;
    if(ctl(fd_.get(),VIDIOC_DQBUF,&b)<0){if(errno==EAGAIN)break;check(-1,"DQBUF");}
    auto& buf=buffers_.at(b.index);if(buf.dequeued) throw std::runtime_error("duplicate DQBUF");buf.dequeued=true;
    if(memory_==V4L2_MEMORY_DMABUF){cpu_access(buf.dma.get(),DMA_BUF_SYNC_START);buf.cpu=true;}
    if(plane.bytesused>buf.size||plane.data_offset>plane.bytesused||plane.bytesused-plane.data_offset<layout.sizeimage) throw std::runtime_error("invalid capture plane bounds");
    if(b.flags&V4L2_BUF_FLAG_ERROR){release(b.index);++raw_skipped;continue;}
    if(have){release(latest.index);++raw_skipped;}
    latest={b.index,b.sequence,b.flags,plane.data_offset,plane.bytesused,now_us(),uint64_t(b.timestamp.tv_sec)*1000000+uint64_t(b.timestamp.tv_usec)};have=true;
  }
  if(!have) throw std::runtime_error("no valid frame after capture wake");
  return latest;
}
void Capture::unchanged() {
  v4l2_dv_timings t{};query_timings(fd_.get(),t,"signal lost");
  v4l2_format f{};f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;check(ctl(fd_.get(),VIDIOC_G_FMT,&f),"format changed");
  auto& a=format_;auto& b=f.fmt.pix_mp;
  if(t.type!=timings_.type||t.bt.interlaced||t.bt.width!=a.width||t.bt.height!=a.height||std::abs(cadence(t)-signal_fps)>0.15||
     a.width!=b.width||a.height!=b.height||a.pixelformat!=b.pixelformat||a.field!=b.field||a.num_planes!=b.num_planes||a.colorspace!=b.colorspace||a.ycbcr_enc!=b.ycbcr_enc||a.quantization!=b.quantization||a.xfer_func!=b.xfer_func||a.plane_fmt[0].bytesperline!=b.plane_fmt[0].bytesperline||a.plane_fmt[0].sizeimage!=b.plane_fmt[0].sizeimage) throw std::runtime_error("HDMI timing/format epoch changed");
}
Capture::~Capture() {
  // Caller MUST destroy Encoder before Capture. Outstanding buffers are never QBUF'd on errors.
  if(streaming_){auto type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;ctl(fd_.get(),VIDIOC_STREAMOFF,&type);}
  for(auto& b:buffers_) if(b.data) munmap(b.data,b.size);
}
} // namespace rkmoon
