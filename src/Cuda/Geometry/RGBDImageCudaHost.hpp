//
// Created by wei on 11/5/18.
//

#include "RGBDImageCuda.h"

namespace open3d {
namespace cuda {
RGBDImageCuda::RGBDImageCuda(float depth_near,
                             float depth_far,
                             float depth_factor)
    : depth_near_(depth_near), depth_far_(depth_far),
      depth_factor_(depth_factor),
      width_(-1), height_(-1), device_(nullptr) {}

RGBDImageCuda::RGBDImageCuda(int width, int height,
                             float depth_near,
                             float depth_far,
                             float depth_factor)
    : depth_near_(depth_near), depth_far_(depth_far),
      depth_factor_(depth_factor) {
    Create(width, height);
}

RGBDImageCuda::RGBDImageCuda(const RGBDImageCuda &other) {
    device_ = other.device_;

    depth_near_ = other.depth_near_;
    depth_far_ = other.depth_far_;
    depth_factor_ = other.depth_factor_;

    depth_raw_ = other.depth_raw_;
    depthf_ = other.depthf_;
    color_ = other.color_;
    intensity_ = other.intensity_;
}

RGBDImageCuda &RGBDImageCuda::operator=(const RGBDImageCuda &other) {
    if (this != &other) {
        Release();

        device_ = other.device_;

        depth_near_ = other.depth_near_;
        depth_far_ = other.depth_far_;
        depth_factor_ = other.depth_factor_;

        depth_raw_ = other.depth_raw_;
        depthf_ = other.depthf_;
        color_ = other.color_;
        intensity_ = other.intensity_;
    }
    return *this;
}

RGBDImageCuda::~RGBDImageCuda() {
    Release();
}

bool RGBDImageCuda::Create(int width, int height) {
    assert(width > 0 && height > 0);

    if (device_ != nullptr) {
        if (width_ != width || height_ != height) {
            PrintError("[RGBDImageCuda] Incompatible image size,"
                       "@Create aborted.\n");
            return false;
        }
        return true;
    }

    device_ = std::make_shared<RGBDImageCudaDevice>();

    width_ = width;
    height_ = height;

    depth_raw_.Create(width, height);
    color_.Create(width, height);
    depthf_.Create(width, height);
    intensity_.Create(width, height);

    UpdateDevice();
    return true;
}

void RGBDImageCuda::Release() {
    device_ = nullptr;

    depth_raw_.Release();
    color_.Release();
    depthf_.Release();
    intensity_.Release();
}

void RGBDImageCuda::Upload(Image &depth_raw, Image &color_raw) {
    assert(depth_raw.width_ == color_raw.width_
               && depth_raw.height_ == color_raw.height_);
    width_ = depth_raw.width_;
    height_ = depth_raw.height_;

    bool success = Create(width_, height_);
    if (success) {
        depth_raw_.Upload(depth_raw);
        color_.Upload(color_raw);
        depth_raw_.ConvertToFloat(depthf_, 1.0f / depth_factor_);
        color_.ConvertRGBToIntensity(intensity_);

        UpdateDevice();
    }
}

void RGBDImageCuda::CopyFrom(RGBDImageCuda &other) {
    if (&other == this) return;
    bool success = Create(other.width_, other.height_);
    if (success) {
        depth_raw_.CopyFrom(other.depth_raw_);
        color_.CopyFrom(other.color_);
        depthf_.CopyFrom(other.depthf_);
        intensity_.CopyFrom(other.intensity_);

        UpdateDevice();
    }
}

void RGBDImageCuda::Build(
    ImageCuda<Vector1s> &depth_raw, ImageCuda<Vector3b> &color_raw) {

    assert(depth_raw.width_ == color_raw.width_
               && depth_raw.height_ == color_raw.height_);
    width_ = depth_raw.width_;
    height_ = depth_raw.height_;

    bool success = Create(width_, height_);
    if (success) {
        depth_raw_.CopyFrom(depth_raw);
        color_.CopyFrom(color_raw);
        depth_raw_.ConvertToFloat(depthf_, 1.0f / depth_factor_);
        color_.ConvertRGBToIntensity(intensity_);

        UpdateDevice();
    }
}

void RGBDImageCuda::UpdateDevice() {
    if (device_ != nullptr) {
        device_->width_ = width_;
        device_->height_ = height_;

        depthf_.UpdateDevice();
        device_->depth_ = *depthf_.device_;

        color_.UpdateDevice();
        device_->color_ = *color_.device_;

        intensity_.UpdateDevice();
        device_->intensity_ = *intensity_.device_;
    }
}

/** Legacy **/
void RGBDImageCuda::Upload(cv::Mat &depth, cv::Mat &color) {
    assert(depth.cols == color.cols && depth.rows == color.rows);
    width_ = depth.cols;
    height_ = depth.rows;

    bool success = Create(width_, height_);
    if (success) {
        depth_raw_.Upload(depth);
        color_.Upload(color);

        depth_raw_.ConvertToFloat(depthf_, 1.0f / depth_factor_);
        color_.ConvertRGBToIntensity(intensity_);

        UpdateDevice();
    }
}
} // cuda
} // open3d