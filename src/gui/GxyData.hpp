// ========================================================================== //
// Copyright (c) 2014-2019 The University of Texas at Austin.                 //
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
  
#include <nodes/NodeDataModel>
#include <QJsonArray>

struct GxyDataInfo
{
  GxyDataInfo()
  {
    name = "none";
    type = -1;
    isVector = true;
    data_min = data_max = 0;
    box[0] = box[1] = box[2] = box[3] = box[4] = box[5] = 0;
  }

  virtual void save(QJsonObject& p) const
  {
    p["dataset"] = QString(name.c_str());
    p["type"] = type;
    p["isVector"] = isVector;
    QJsonArray rangeJson = { data_min, data_max };
    p["data range"] = rangeJson;

    for (auto i = 0; i < 6; i++)
      p["box"].toArray().push_back(box[i]);
  }

  virtual void restore(QJsonObject const &p)
  {
    name = p["dataset"].toString().toStdString();
    type = p["type"].toInt();
    isVector = p["isVector"].toBool();
    data_min = p["data range"][0].toDouble();
    data_max = p["data range"][1].toDouble();
    for (auto i = 0; i < 6; i++)
      box[i] = p["box"][i].toDouble();
  }

  void print()
  {
    std::cerr << "name: " << name << "\n";
    std::cerr << "type: " << type << "\n";
    std::cerr << "isVector: " << isVector << "\n";
    std::cerr << "range: " << data_min << " " << data_max << "\n";
    std::cerr << "box: " << box[0] << " " << box[1] << " " << box[2] << " " << box[3] << " " << box[4] << " " << box[5] << "\n";
  }

  std::string name;
  int type;
  bool isVector;
  float data_min, data_max;
  float box[6];
};

class GxyGuiObject : public QtNodes::NodeData
{
public:
  
  GxyGuiObject() {}
  GxyGuiObject(std::string o) { origin = o; }
  
  QtNodes::NodeDataType type() const override
  { 
    return QtNodes::NodeDataType {"gxygui", "GxyGui"};
  }

  virtual void print()
  {
    std::cerr << "origin: " << origin << "\n";
  }

  std::string get_origin() { return origin; }

private:

  std::string origin;
};


class GxyData : public GxyGuiObject
{
public:
  GxyData(GxyDataInfo& di) : di(di), GxyGuiObject() {}
  GxyData() : GxyGuiObject() {}

  GxyData(std::string o) : GxyGuiObject(o) {}
  
  void print() override
  {
    GxyGuiObject::print();
    di.print();
  }

  QtNodes::NodeDataType type() const override
  {
    return QtNodes::NodeDataType {"gxygui", "GxyGui"};
  }

  virtual void save(QJsonObject& p) const
  {
    di.save(p);
  }

  virtual void restore(QJsonObject const &p)
  {
    di.restore(p);
  }

  GxyDataInfo di;
};
