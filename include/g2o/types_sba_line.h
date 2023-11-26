// This file is part of PLVS
// Copyright (C) 2018-present Luigi Freda <luigifreda at gmail dot com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#pragma once

#ifdef USE_G2O_NEW
#include "Thirdparty/g2o_new/install/include/g2o/core/base_vertex.h"
#else
#include "Thirdparty/g2o/g2o/core/base_vertex.h"
#endif 

#include <Eigen/Geometry>
#include <iostream>

namespace g2o {

typedef Eigen::Matrix<double, 6, 1> Vector6d;

/**
 * \brief Line represented with a Vector6d which combines the two 3D (XYZ) vertices [pstart_xyz, pend_xyz] 
 */
class VertexSBALine : public BaseVertex<6, Vector6d>
{
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW    
    VertexSBALine();
    virtual bool read(std::istream& is);
    virtual bool write(std::ostream& os) const;

    virtual void setToOriginImpl() {
      _estimate.fill(0.);
    }

    virtual void oplusImpl(const double* update) {
      Eigen::Map<const Vector6d> v(update);
      _estimate += v;
      //project();
    }

    void setInitialLength(const double l){
      initialLength = l; 
    }

    // used to check if the length diverged
    bool isBad(const double factor=2.0) const {
      if(initialLength>0.){// if we set the initial length
        const double newLength = (_estimate.head(3)-_estimate.tail(3)).norm();
        return (newLength > factor * initialLength) || (newLength <  initialLength/factor);
      } else {
        return false; 
      }
    }

#if 0
    void setEndPoints(const Eigen::Vector3d& P_, const Eigen::Vector3d& Q_) {
      P = P_;
      Q = Q_; 
    }

    void project() {
      //  project starting points P and Q on new line and use them as update
      const Eigen::Vector3d Pnew = _estimate.head(3);
      const Eigen::Vector3d Qnew = _estimate.tail(3);
      
      Eigen::Vector3d dir = (Pnew - Qnew);
      dir.normalize();
      
      const double dotProdP = (P-Pnew).dot(dir);
      const double dotProdQ = (Q-Qnew).dot(dir);
      
      _estimate[0] = Pnew[0] + dir[0]*dotProdP;
      _estimate[1] = Pnew[1] + dir[1]*dotProdP;
      _estimate[2] = Pnew[2] + dir[2]*dotProdP;
      
      _estimate[3] = Qnew[0] + dir[0]*dotProdQ;
      _estimate[4] = Qnew[1] + dir[1]*dotProdQ;
      _estimate[5] = Qnew[2] + dir[2]*dotProdQ;
    }

    Eigen::Vector3d P;  // initial P (start)
    Eigen::Vector3d Q;  // initial Q (end)
#endif     

    double initialLength = 0.;
};


} // end namespace

