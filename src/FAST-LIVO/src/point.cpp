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
#include <vikit/math_utils.h>
#include <point.h>

namespace lidar_selection
{

    int Point::point_counter_ = 0;

    Point::Point(const Vector3d &pos) : id_(point_counter_++),
                                        pos_(pos),
                                        normal_set_(false),
                                        n_obs_(0),
                                        last_published_ts_(0),
                                        last_projected_kf_id_(-1),
                                        // type_(TYPE_UNKNOWN),
                                        n_failed_reproj_(0),
                                        n_succeeded_reproj_(0),
                                        last_structure_optim_(0),
                                        have_scaled(false)
    {
    }

    Point::Point(const Vector3d &pos, FeaturePtr ftr) : id_(point_counter_++),
                                                        pos_(pos),
                                                        normal_set_(false),
                                                        n_obs_(1),
                                                        last_published_ts_(0),
                                                        last_projected_kf_id_(-1),
                                                        // type_(TYPE_UNKNOWN),
                                                        n_failed_reproj_(0),
                                                        n_succeeded_reproj_(0),
                                                        last_structure_optim_(0),
                                                        have_scaled(false)
    {
        obs_.push_front(ftr);
    }

    Point::~Point()
    {
        // printf("The point %d has been destructed.", id_);
        std::for_each(obs_.begin(), obs_.end(), [&](FeaturePtr i)
                      { i.reset(); });
    }

    void Point::addFrameRef(FeaturePtr ftr) //添加特征到被观察列表
    {
        obs_.push_front(ftr);
        ++n_obs_;
    }

    FeaturePtr Point::findFrameRef(Frame *frame)    //查找某一帧是否在点的被观测列表中
    {
        for (auto it = obs_.begin(), ite = obs_.end(); it != ite; ++it)
            if ((*it)->frame == frame)
                return *it;
        return nullptr; // no keyframe found
    }

    bool Point::deleteFrameRef(Frame *frame)    //删除某一帧的特征
    {
        for (auto it = obs_.begin(), ite = obs_.end(); it != ite; ++it)
        {
            if ((*it)->frame == frame)
            {
                obs_.erase(it); // hr: delete point of the frame
                return true;
            }
        }
        return false;
    }

    void Point::deleteFeatureRef(FeaturePtr ftr)    //删除某一特征(指针输入)
    {
        for (auto it = obs_.begin(), ite = obs_.end(); it != ite; ++it)
        {
            if ((*it) == ftr)
            {
                obs_.erase(it);
                return;
            }
        }
    }

    void Point::initNormal()
    {
        assert(!obs_.empty());
        const FeaturePtr ftr = obs_.back(); // hr: get the first point
        assert(ftr->frame != nullptr);
        normal_ = ftr->frame->T_f_w_.rotation_matrix().transpose() * (-ftr->f); // f:Unit-bearing vector of the feature. TODO:world frame?
        // pos():Return the pose of the frame in the (w)orld coordinate frame. TODO:公式来源？
        normal_information_ = DiagonalMatrix<double, 3, 3>(pow(20 / (pos_ - ftr->frame->pos()).norm(), 2), 1.0, 1.0);
        normal_set_ = true;
    }

    bool Point::getClosePose(const FramePtr &new_frame, FeaturePtr &ftr) const  //得到当前观察帧距离最近的观察帧
    {

        if (obs_.size() <= 0)
            return false;

        auto min_it = obs_.begin();
        double min_cos_angle = 3.14;
        for (auto it = obs_.begin(), ite = obs_.end(); it != ite; ++it)//遍历所有观测帧
        {
            SE3 delta_pose = (*it)->T_f_w_ * new_frame->T_f_w_.inverse(); //dir.normalize();//新帧到旧帧的delta位姿
            double delta_theta = (delta_pose.rotation_matrix().trace() > 3.0 - 1e-6) ? 0.0 : std::acos(0.5 * (delta_pose.rotation_matrix().trace() - 1));
            double delta_p = delta_pose.translation().norm();//; 新旧帧之间的位移距离
            double p_in_ref = ((*it)->T_f_w_ * pos_).norm();//这个点到之前的观测帧的距离
            if (delta_p > p_in_ref * 0.8)
                continue;
            if (delta_theta < min_cos_angle)
            {
                min_cos_angle = delta_theta;
                min_it = it;
            }
        }
        ftr = *min_it;

        if (min_cos_angle > 2.0) // assume that observations larger than 60° are useless 0.5
        {
            // ROS_ERROR("The obseved angle is larger than 60°.");
            return false;
        }

        return true;
    }

    /**
     * @brief 传入相机系的原点，计算当前点Point和相机系观测的方向最近的那个观测帧位置，
     */
    bool Point::getCloseViewObs(const Vector3d &framepos, FeaturePtr &ftr, const Vector2d &cur_px) const
    {
        // TODO: get frame with same point of view AND same pyramid level!
        //; 如果当前点没有观测的patch，那么直接退出
        if (obs_.size() <= 0)
            return false;

        Vector3d obs_dir(framepos - pos_);   //; 当前point在相机系下的观测方向，注意仍然是在world系下表示的
        obs_dir.normalize();
        auto min_it = obs_.begin();
        double min_cos_angle = 0;

        for (auto it = obs_.begin(), ite = obs_.end(); it != ite; ++it)
        {
            //; (*it)->T_f_w_.inverse().translation()是这个观测的patch的图像的相机系在world系下的位置
            Vector3d dir((*it)->T_f_w_.inverse().translation() - pos_);  //; 这个patch对过去的观测点地图点的观测方向
            dir.normalize();
            double cos_angle = obs_dir.dot(dir);
            if (cos_angle > min_cos_angle)  // 寻找最近的观测角度的patch
            {
                min_cos_angle = cos_angle;
                min_it = it;
            }
        }
        ftr = *min_it;

        // Vector2d ftr_px = ftr->px;
        // double pixel_dist = (cur_px-ftr_px).norm();

        // if(pixel_dist > 200)
        // {
        //   ROS_ERROR("The pixel dist exceeds 200.");
        //   return false;
        // }

        //; 如果观测角度 > 60度，那么也不要这个观测
        if (min_cos_angle < 0.5) // assume that observations larger than 60° are useless 0.5
        {
            // ROS_ERROR("The obseved angle is larger than 60°.");
            return false;
        }

        return true;
    }


    bool Point::getCloseViewObs_test(const Vector3d &framepos, FeaturePtr &ftr, const Vector2d &cur_px,
                                     double &min_cos_angle) const
    {
        // TODO: get frame with same point of view AND same pyramid level!
        if (obs_.size() <= 0)
            return false;

        Vector3d obs_dir(framepos - pos_);
        obs_dir.normalize();
        auto min_it = obs_.begin();
        min_cos_angle = 0;

        for (auto it = obs_.begin(), ite = obs_.end(); it != ite; ++it)
        {
            Vector3d dir((*it)->T_f_w_.inverse().translation() - pos_);
            dir.normalize();
            double cos_angle = obs_dir.dot(dir);
            if (cos_angle > min_cos_angle)
            {
                min_cos_angle = cos_angle;
                min_it = it;
            }
        }
        ftr = *min_it;

        // Vector2d ftr_px = ftr->px;
        // double pixel_dist = (cur_px-ftr_px).norm();

        // if(pixel_dist > 200)
        // {
        //   ROS_ERROR("The pixel dist exceeds 200.");
        //   return false;
        // }

        if (min_cos_angle < 0.5) // assume that observations larger than 60° are useless 0.5
        {
            // ROS_ERROR("The obseved angle is larger than 60°.");
            return false;
        }

        return true;
    }

    //拿到距离帧位置最远的观测点
    void Point::getFurthestViewObs(const Vector3d &framepos, FeaturePtr &ftr) const
    {
        // Vector3d obs_dir(framepos - pos_); obs_dir.normalize();
        // auto max_it=obs_.begin();
        // double max_cos_angle = 1;
        // for(auto it=obs_.begin(), ite=obs_.end(); it!=ite; ++it)
        // {
        //   Vector3d dir((*it)->T_f_w_.inverse().translation() - pos_); dir.normalize();
        //   double cos_angle = obs_dir.dot(dir);
        //   if(cos_angle < max_cos_angle)
        //   {
        //     max_cos_angle = cos_angle;
        //     max_it = it;
        //   }
        // }
        Vector3d obs_dir(framepos - pos_);
        obs_dir.normalize();
        auto max_it = obs_.begin();
        double maxdist = 0.0;
        for (auto it = obs_.begin(), ite = obs_.end(); it != ite; ++it)
        {
            double dist = ((*it)->T_f_w_.inverse().translation() - framepos).norm();
            if (dist > maxdist)
            {
                maxdist = dist;
                max_it = it;
            }
        }
        ftr = *max_it;
    }


    //优化特征点的空间位置,基于重投影误差最小化
    void Point::optimize(const size_t n_iter)
    {
        Vector3d old_point = pos_;
        double chi2 = 0.0;
        Matrix3d A;
        Vector3d b;

        for (size_t i = 0; i < n_iter; i++)
        {
            A.setZero();
            b.setZero();
            double new_chi2 = 0.0;

            // compute residuals
            for (auto it = obs_.begin(); it != obs_.end(); ++it)
            {
                Matrix23d J;
                const Vector3d p_in_f((*it)->frame->T_f_w_ * pos_);
                Point::jacobian_xyz2uv(p_in_f, (*it)->frame->T_f_w_.rotation_matrix(), J);
                const Vector2d e(vk::project2d((*it)->f) - vk::project2d(p_in_f));  //归一化误差距离，把Z轴归一化

                if ((*it)->type == Feature::EDGELET)    //如果是边缘点，使用加权(信息矩阵)后的高斯牛顿优化
                {
                    float err_edge = (*it)->grad.transpose() * e;
                    new_chi2 += err_edge * err_edge;
                    A.noalias() += J.transpose() * (*it)->grad * (*it)->grad.transpose() * J;
                    b.noalias() -= J.transpose() * (*it)->grad * err_edge;
                }
                else//直接高斯牛顿法
                {
                    new_chi2 += e.squaredNorm();
                    A.noalias() += J.transpose() * J;   //H矩阵
                    b.noalias() -= J.transpose() * e;   //g矩阵
                }
            }

            // solve linear system
            const Vector3d dp(A.ldlt().solve(b));

            // check if error increased
            if ((i > 0 && new_chi2 > chi2) || (bool)std::isnan((double)dp[0]))
            {
#ifdef POINT_OPTIMIZER_DEBUG
                cout << "it " << i
                     << "\t FAILURE \t new_chi2 = " << new_chi2 << endl;
#endif
                pos_ = old_point; // roll-back
                break;
            }

            // update the model
            Vector3d new_point = pos_ + dp;
            old_point = pos_;
            pos_ = new_point;
            chi2 = new_chi2;
#ifdef POINT_OPTIMIZER_DEBUG
            cout << "it " << i
                 << "\t Success \t new_chi2 = " << new_chi2
                 << "\t norm(b) = " << vk::norm_max(b)
                 << endl;
#endif

            // stop when converged
            if (vk::norm_max(dp) <= 0.0000000001)
                break;
        }
#ifdef POINT_OPTIMIZER_DEBUG
        cout << endl;
#endif
    }

} // namespace svo
