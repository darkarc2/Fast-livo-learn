// This file is part of SVO - Semi-direct Visual Odometry.
//
// Copyright (C) 2014 Christian Forster <forster at ifi dot uzh dot ch>
// (Robotics and Perception Group, University of Zurich, Switzerland).
//
// SVO is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or any later version.
//
// SVO is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <stdexcept>
#include <frame.h>
#include <feature.h>
#include <point.h>
#include <boost/bind.hpp>
#include <vikit/math_utils.h>
#include <vikit/vision.h>
#include <vikit/performance_monitor.h>
// #include <fast/fast.h>

namespace lidar_selection
{

    int Frame::frame_counter_ = 0;

    // 初始化Frame构造函数，传入id，相机模型，5个关键点，是否为关键帧，然后进行初始化
    Frame::Frame(vk::AbstractCamera *cam, const cv::Mat &img) : 
        id_(frame_counter_++),  //; frame_id
        cam_(cam),
        key_pts_(5),
        is_keyframe_(false)
    {
        initFrame(img);
    }

    Frame::~Frame()
    {
        std::for_each(fts_.begin(), fts_.end(), [&](FeaturePtr i)
                      { i.reset(); });
    }

    void Frame::initFrame(const cv::Mat &img)
    {
        // check image
        if (img.empty() || img.type() != CV_8UC1 || img.cols != cam_->width() || img.rows != cam_->height())
            throw std::runtime_error(
                "Frame: provided image has not the same size as the camera model or image is not grayscale");

        // Set keypoints to nullptr
        std::for_each(key_pts_.begin(), key_pts_.end(), [&](FeaturePtr ftr)
                      { ftr = nullptr; });
        // ImgPyr: vector<cv::Mat> 用swap交换到一个新的类型的vector,
        // 将原来的a拷贝出去，然后自然销毁，而新的到的a是全新的没有存任何数据的。
        ImgPyr().swap(img_pyr_);   //; 图像金字塔的值复位
        img_pyr_.push_back(img);   //; 然后把当前帧的图像加到图像金字塔中，也就是图像金字塔的第0层
        // Build Image Pyramid
        // frame_utils::createImgPyramid(img, max(Config::nPyrLevels(), Config::kltMaxLevel()+1), img_pyr_);
        // frame_utils::createImgPyramid(img, 5, img_pyr_);
    }

    void Frame::setKeyframe()   // 设置关键帧
    {
        is_keyframe_ = true;
        setKeyPoints();
    }

    void Frame::addFeature(FeaturePtr ftr) // 添加特征
    {
        fts_.push_back(ftr);
    }
    
    void Frame::setKeyPoints()  // 设置关键点
    {
        for (size_t i = 0; i < 5; ++i)
            if (key_pts_[i] != nullptr)
                if (key_pts_[i]->point == nullptr)
                    key_pts_[i] = nullptr;
                    
        std::for_each(fts_.begin(), fts_.end(),
                      [&](FeaturePtr ftr)
                      {
                          if (ftr->point != nullptr)
                              checkKeyPoints(ftr);
                      }); // hr: fts_:List of features in the image//把图像中的特征点都遍历一遍，然后判断是否为关键点
    }

    // 找五点法中的5个点，五个点在一个平面上，如下图注释所示
    void Frame::checkKeyPoints(FeaturePtr ftr)
    {
        const int cu = cam_->width() / 2;
        const int cv = cam_->height() / 2;

        /* 举例：选取的是这5个点
         * 以key_pts_[2]为例：
         *
         * 		|-----------------------|
         * 		|*		      		   *|
         * 		|			   			|
         * 		|	  	    *		    |
         * 		|		            ？	|
         * 		|*		               *| <-key_pts_[2],要求这里的点在减去cu,cv之后面积仍然是最大的，
         * 		|-----------------------|               因此可以判定为在最右下角的点满足条件
         */

        // center pixel
        if (key_pts_[0] == nullptr)
            key_pts_[0] = ftr;
        //如果比之前的点更靠近中心，则更新
        else if (std::max(std::fabs(ftr->px[0] - cu), std::fabs(ftr->px[1] - cv)) < std::max(std::fabs(key_pts_[0]->px[0] - cu), std::fabs(key_pts_[0]->px[1] - cv)))
            key_pts_[0] = ftr; // 不断更新，选择最靠近图像中心的点

        // 右上角
        if (ftr->px[0] >= cu && ftr->px[1] >= cv)
        {
            if (key_pts_[1] == nullptr)
                key_pts_[1] = ftr;
            else if ((ftr->px[0] - cu) * (ftr->px[1] - cv) > (key_pts_[1]->px[0] - cu) * (key_pts_[1]->px[1] - cv))
                key_pts_[1] = ftr; // 相乘面积最大：说明点越靠近边界，满足条件
        }

        // 右下角
        if (ftr->px[0] >= cu && ftr->px[1] < cv)
        {
            if (key_pts_[2] == nullptr)
                key_pts_[2] = ftr;
            // else if((ftr->px[0]-cu) * (ftr->px[1]-cv)
            else if ((ftr->px[0] - cu) * (cv - ftr->px[1])
                     // > (key_pts_[2]->px[0]-cu) * (key_pts_[2]->px[1]-cv))
                     > (key_pts_[2]->px[0] - cu) * (cv - key_pts_[2]->px[1]))
                key_pts_[2] = ftr;
        }

        // 左下角
        if (ftr->px[0] < cu && ftr->px[1] < cv)
        {
            if (key_pts_[3] == nullptr)
                key_pts_[3] = ftr;
            else if ((ftr->px[0] - cu) * (ftr->px[1] - cv) > (key_pts_[3]->px[0] - cu) * (key_pts_[3]->px[1] - cv))
                key_pts_[3] = ftr;
        }

        // 左上角
        if (ftr->px[0] < cu && ftr->px[1] >= cv)
        // if(ftr->px[0] < cv && ftr->px[1] >= cv)
        {
            if (key_pts_[4] == nullptr)
                key_pts_[4] = ftr;

            else if (cu - (ftr->px[0]) * (ftr->px[1] - cv) > (cu - key_pts_[4]->px[0]) * (key_pts_[4]->px[1] - cv))
                key_pts_[4] = ftr;
        }
    }

    void Frame::removeKeyPoint(FeaturePtr ftr)// 移除关键点
    {
        bool found = false;
        std::for_each(key_pts_.begin(), key_pts_.end(), [&](FeaturePtr &i)
                      {
                          if (i == ftr)
                          {
                              i = nullptr;
                              found = true;
                          }
                      });
        if (found)
            setKeyPoints(); //重新设置关键点
    }

    bool Frame::isVisible(const Vector3d &xyz_w) const // 判断点是否在视野内
    {
        Vector3d xyz_f = T_f_w_ * xyz_w; // from world to camera frame

        if (xyz_f.z() < 0.0)
            return false;         // point is behind the camera
        Vector2d px = f2c(xyz_f); // 转换到像素

        if (px[0] >= 0.0 && px[1] >= 0.0 && px[0] < cam_->width() && px[1] < cam_->height())
            return true; // in current camera frame
        return false;
    }

    /// Utility functions for the Frame class
    namespace frame_utils
    {
        // 创建图像金字塔，传入的参数是第一层的图像（底层），层数，金字塔
        void createImgPyramid(const cv::Mat &img_level_0, int n_levels, ImgPyr &pyr)
        {
            pyr.resize(n_levels);
            pyr[0] = img_level_0; // 原图

            // 遍历层数，下采样1/4
            for (int i = 1; i < n_levels; ++i)
            {
                pyr[i] = cv::Mat(pyr[i - 1].rows / 2, pyr[i - 1].cols / 2, CV_8U);
                vk::halfSample(pyr[i - 1], pyr[i]);// 缩小图像
            }
        }

        // 获得场景深度，传入，帧，深度均值，深度最小值，好像没用到
        // bool getSceneDepth(const Frame &frame, double &depth_mean, double &depth_min)
        // {
        //     vector<double> depth_vec;
        //     depth_vec.reserve(frame.fts_.size());
        //     depth_min = std::numeric_limits<double>::max();
        //     for (auto it = frame.fts_.begin(), ite = frame.fts_.end(); it != ite; ++it)
        //     {
        //         if ((*it)->point != nullptr)
        //         {
        //             const double z = frame.w2f((*it)->point->pos_).z(); // world-frame (w) to camera-frams
        //             depth_vec.push_back(z);
        //             depth_min = fmin(z, depth_min); // f:专门用于浮点数？接受两个参数并返回其中最小的一个。如果其中一个参数是 NaN，则返回另一个参数
        //         }
        //     }
        //     if (depth_vec.empty())
        //     {
        //         cout << "Cannot set scene depth. Frame has no point-observations!" << endl;
        //         return false;
        //     }
        //     depth_mean = vk::getMedian(depth_vec); // 获得深度均值
        //     return true;
        // }

    } // namespace frame_utils
} // namespace lidar_selection
