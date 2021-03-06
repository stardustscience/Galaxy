// ========================================================================== //
// Copyright (c) 2014-2020 The University of Texas at Austin.                 //
// All rights reserved.                                                       //
//                                                                            //
// Licensed under the Apache License, Version 2.0 (the "License");            //
// you may not use this file except in compliance with the License.           //
// A copy of the License is included with this software in the file LICENSE.  //
// If your copy does not contain the License, you may obtain a copy of the    //
// License at:                                                                //
//                                                                            //
//     https://www.apache.org/licenses/LICENSE-2.0                            //
//                                                                            //
// Unless required by applicable law or agreed to in writing, software        //
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT  //
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.           //
// See the License for the specific language governing permissions and        //
// limitations under the License.                                             //
//                                                                            //
// ========================================================================== //

#pragma once
  
#include <iostream>
#include "Vis.hpp"

class VolumeVis : public Vis
{
public:
  
  VolumeVis() : Vis() {}
  VolumeVis(std::string o) : Vis(o) {}

  QtNodes::NodeDataType type() const override
  { 
    return QtNodes::NodeDataType {"vis", "VIS"};
  }

  virtual void print() override
  {
    Vis::print();
    std::cerr << "slicing planes: \n";
    for (auto s : slices)
      std::cerr << "    " << s.x << " " << s.y << " " << s.z << " " << s.w << "\n";
    std::cerr << "isovalues: \n";
    for (auto i : isovalues)
      std::cerr << "    " << i << "\n";
  }

  void toJson(QJsonObject& p) override
  {
    Vis::toJson(p);

    p["type"] = "VolumeVis";

    QJsonArray isovaluesJson;

    for (auto isoval : isovalues)
      isovaluesJson.push_back(isoval);

    p["isovalues"] = isovaluesJson;

    QJsonArray slicesJson;
    for (auto slice : slices)
    {
      QJsonArray sliceJson;
      sliceJson.push_back(slice.x);
      sliceJson.push_back(slice.y);
      sliceJson.push_back(slice.z);
      sliceJson.push_back(slice.w);
      slicesJson.push_back(sliceJson);
    }
    p["slices"] = slicesJson;

    p["volume rendering"]  = volume_rendering_flag;
  }

  void fromJson(QJsonObject p) override
  {
    Vis::fromJson(p);

    QJsonArray isovaluesJson = p["isovalues"].toArray();

    for (auto isovalueJson : isovaluesJson)
      isovalues.push_back(isovalueJson.toDouble());

    QJsonArray slicesJson = p["slices"].toArray();

    for (auto sliceJson : slicesJson)
    {
      QJsonArray o = sliceJson.toArray();
      gxy::vec4f slice;
      slice.x = o[0].toDouble();
      slice.y = o[1].toDouble();
      slice.z = o[2].toDouble();
      slice.w = o[3].toDouble();
      slices.push_back(slice);
    }

    volume_rendering_flag = p["volume rendering"].toBool();
  }

  std::vector<gxy::vec4f> slices;
  std::vector<float> isovalues;
  bool volume_rendering_flag;
  float xfer_range_min, xfer_range_max;
};

