#pragma once
// Super-resolution for the resize dialog (nResizeSRModel 0 = ESPCN, 1 = FSRCNN, x2..x4):
// runs the public OpenCV dnn_superres TensorFlow models (Apache-2.0, embedded as resources) with a
// small built-in graph interpreter (Conv2D/Add/BiasAdd/Relu/Abs/Sub/Mul/Tanh/DepthToSpace).
// Like OpenCV, the network upsamples the Y channel; Cb/Cr are upscaled bicubically.
#include "core/Image.h"

#include <memory>
#include <string>
#include <vector>

namespace bg {

class SrModel {
public:
    SrModel();
    ~SrModel();
    bool load(const std::vector<uint8_t>& pb, std::string& err);
    int scale() const;
    ImagePtr upscale(const Image& src) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace bg
