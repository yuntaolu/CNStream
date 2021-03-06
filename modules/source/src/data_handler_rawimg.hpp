/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#ifndef MODULES_SOURCE_HANDLER_RAWIMGMEM_HPP_
#define MODULES_SOURCE_HANDLER_RAWIMGMEM_HPP_

#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>

#ifdef HAVE_OPENCV
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#if (CV_MAJOR_VERSION >= 3)
#include "opencv2/imgcodecs/imgcodecs.hpp"
#endif
#endif

#include "data_handler_util.hpp"
#include "data_source.hpp"
#include "ffmpeg_decoder.hpp"
#include "cnstream_logging.hpp"

#define DEFAULT_MODULE_CATEGORY SOURCE

namespace cnstream {

/*raw image packet struct
 */
typedef struct {
  unsigned char *data = nullptr;
  CNDataFormat pixel_fmt = CN_INVALID;
  int size = 0;
  int width = 0;
  int height = 0;
  uint64_t pts = 0;
  uint32_t flags = 0;
  enum {
    FLAG_EOS = 0x01,
  };
} ImagePacket;

class RawImgMemHandlerImpl : public IHandler {
 public:
  explicit RawImgMemHandlerImpl(DataSource *module, RawImgMemHandler &handler)  // NOLINT
      : module_(module), handler_(handler) {
    stream_id_ = handler_.GetStreamId();
  }

  ~RawImgMemHandlerImpl() {}

  bool Open();
  void Close();

#ifdef HAVE_OPENCV
  /**
   * @Note This function will deprecate, use Write(cv::Mat *mat_data, uint64_t pts) instead.
   * @brief Sends raw image with cv::Mat. Only BGR data with 8UC3 type is supported, and data is continuous.
   * @param
         - mat_data: cv::Mat pointer with bgr24 format image data, feed mat_data as nullptr when feed data end.
   * @retval 0: The data is write successfully,
   * @retval -1: Write failed, maybe eos got or handler is closed.
   * @retval -2: Invalid data.
   */
  int Write(cv::Mat *mat_data);
  /**
   * @brief Sends raw image with cv::Mat. Only BGR data with 8UC3 type is supported, and data is continuous.
   * @param
         - mat_data: cv::Mat pointer with bgr24 format image data, feed mat_data as nullptr when feed data end.
         - pts: pts for image, should be different for each image
   * @retval 0: The data is write successfully,
   * @retval -1: Write failed, maybe eos got or handler is closed.
   * @retval -2: Invalid data.
   */
  int Write(cv::Mat *mat_data, uint64_t pts);
#endif
  /**
   * @Note This function will deprecate, use Write(unsigned char *data, int size, uint64_t pts,
   int width, int height, CNDataFormat pixel_fmt) instead.
   * @brief Sends raw image with image data and image infomation, support formats: bgr24, rgb24, nv21 and nv12.
   bgr24/rgb24/nv21/nv12 format).
   * @param
          - data: image data pointer(one continuous buffer), feed data as nullptr and size as 0 when feed data end
          - size: image data size
          - w: image width
          - h: image height
          - pixel_fmt: image pixel format, support bgr24/rgb24/nv21/nv12 format.
   * @retval 0: The data is write successfully,
   * @retval -1: Write failed, maybe eos got or handler is closed.
   * @retval -2: Invalid data.
   */
  int Write(unsigned char *data, int size, int w = 0, int h = 0, CNDataFormat pixel_fmt = CN_INVALID);
  /**
   * @brief Sends raw image with image data and image infomation, support formats: bgr24, rgb24, nv21 and nv12.
   bgr24/rgb24/nv21/nv12 format).
   * @param
          - data: image data pointer(one continuous buffer), feed data as nullptr and size as 0 when feed data end
          - size: image data size
          - pts: pts for image, should be different for each image
          - w: image width
          - h: image height
          - pixel_fmt: image pixel format, support bgr24/rgb24/nv21/nv12 format.
   * @retval 0: The data is write successfully,
   * @retval -1: Write failed, maybe eos got or handler is closed.
   * @retval -2: Invalid data.
   */
  int Write(unsigned char *data, int size, uint64_t pts, int w = 0, int h = 0, CNDataFormat pixel_fmt = CN_INVALID);

 private:
  DataSource *module_ = nullptr;
  RawImgMemHandler &handler_;
  std::string stream_id_;
  DataSourceParam param_;
  size_t interval_ = 1;

 private:
#ifdef UNIT_TEST
 public:  // NOLINT
#endif
  bool CheckRawImageParams(unsigned char *data, int size, int w, int h, CNDataFormat pixel_fmt);
  bool PrepareConvertCtx(ImagePacket *img_pkt);
  bool CvtColorWithStride(ImagePacket *img_pkt, uint8_t *dst_nv12, int dst_stride);

  bool Process();
  void ProcessLoop();
  bool ProcessOneFrame(ImagePacket *img_pkt);

 private:
  std::atomic<int> running_{0};
  std::thread thread_;
  bool eos_sent_ = false;
  std::atomic<bool> eos_got_{false};
  BoundedQueue<ImagePacket> *img_pktq_ = nullptr;
  /**
   * Ensure that the img_pktq_ is not deleted when the push is blocked.
   */
  std::mutex img_pktq_mutex_;

  uint64_t pts_ = 0;
  uint64_t frame_id_ = 0;

#ifdef HAVE_OPENCV
  cv::Mat *src_mat_ = nullptr;  // src mat with bgr24 or rgb24 format
  cv::Mat *dst_mat_ = nullptr;  // dst mat with I420 format
#endif
  int src_width_ = 0;
  int src_height_ = 0;
  CNDataFormat src_fmt_ = CNDataFormat::CN_INVALID;

 public:
  void SendFlowEos() {
    if (eos_sent_) return;
    auto data = CreateFrameInfo(true);
    if (!data) {
      MLOG(ERROR) << "SendFlowEos: Create CNFrameInfo failed while received eos. stream id is " << stream_id_;
      return;
    }
    SendFrameInfo(data);
    eos_sent_ = true;
  }

  std::shared_ptr<CNFrameInfo> CreateFrameInfo(bool eos = false) { return handler_.CreateFrameInfo(eos); }

  bool SendFrameInfo(std::shared_ptr<CNFrameInfo> data) { return handler_.SendData(data); }

  const DataSourceParam &GetDecodeParam() const { return param_; }
};  // class RawImgMemHandlerImpl

}  // namespace cnstream

#undef DEFAULT_MODULE_CATEGORY
#endif  // MODULES_SOURCE_HANDLER_RAWIMGMEM_HPP_
