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

#include <unistd.h>

#include "VisModel.hpp"
#include <QSignalBlocker>

VisModel::VisModel() 
{
  QFrame *frame  = new QFrame;
  QVBoxLayout *layout = new QVBoxLayout;
  layout->setSpacing(0);
  layout->setContentsMargins(2, 0, 2, 0);
  frame->setLayout(layout);

  QFrame *cmap_box = new QFrame;
  QHBoxLayout *cmap_box_layout = new QHBoxLayout;
  cmap_box->setLayout(cmap_box_layout);

  cmap_box_layout->addWidget(new QLabel("color map"));

  cmap_widget = new QLineEdit();
  cmap_box_layout->addWidget(cmap_widget);
  
  QPushButton *cmap_browse_button = new QPushButton("...");
  cmap_box_layout->addWidget(cmap_browse_button);
  connect(cmap_browse_button, SIGNAL(released()), this, SLOT(openCmapSelectorDialog()));

  layout->addWidget(cmap_box);

  QFrame *cmap_range_w = new QFrame;
  QGridLayout *cmap_range_l = new QGridLayout();
  cmap_range_l->setSpacing(0);
  cmap_range_l->setContentsMargins(2, 0, 2, 0);
  cmap_range_w->setLayout(cmap_range_l);

  cmap_range_l->addWidget(new QLabel("min"), 0, 0);
  cmap_range_min = new QLineEdit;
  cmap_range_min->setText("0");
  cmap_range_min->setValidator(new QDoubleValidator());
  cmap_range_l->addWidget(cmap_range_min, 0, 1);
  connect(cmap_range_min, SIGNAL(editingFinished()), this, SLOT(onCmapMinTextChanged()));
  cmap_range_min_slider = new QSlider(Qt::Horizontal);
  cmap_range_min_slider->setMinimum(0);
  cmap_range_min_slider->setMaximum(1000);
  cmap_range_min_slider->setSliderPosition(0);
  connect(cmap_range_min_slider, SIGNAL(valueChanged(int)), this, SLOT(onCmapMinSliderChanged(int)));
  cmap_range_l->addWidget(cmap_range_min_slider, 0, 2);
  
  cmap_range_l->addWidget(new QLabel("max"), 1, 0);
  cmap_range_max = new QLineEdit;
  cmap_range_max->setText("0");
  cmap_range_max->setValidator(new QDoubleValidator());
  cmap_range_l->addWidget(cmap_range_max, 1, 1);
  connect(cmap_range_max, SIGNAL(editingFinished()), this, SLOT(onCmapMaxTextChanged()));
  cmap_range_max_slider = new QSlider(Qt::Horizontal);
  cmap_range_max_slider->setMinimum(0);
  cmap_range_max_slider->setMaximum(1000);
  cmap_range_max_slider->setSliderPosition(1000);
  connect(cmap_range_max_slider, SIGNAL(valueChanged(int)), this, SLOT(onCmapMaxSliderChanged(int)));
  cmap_range_l->addWidget(cmap_range_max_slider, 1, 2);
  
  QPushButton *resetDataRange = new QPushButton("reset");
  connect(resetDataRange, SIGNAL(released()), this, SLOT(onDataRangeReset()));
  cmap_range_l->addWidget(resetDataRange, 2, 0);

  layout->addWidget(cmap_range_w);

  _properties->addProperties(frame);

  connect(cmap_range_max, SIGNAL(editingFinished()), this, SLOT(enableIfValid()));
  connect(cmap_range_min, SIGNAL(editingFinished()), this, SLOT(enableIfValid()));
  connect(_properties->getApplyButton(), SIGNAL(released()), this, SLOT(onApply()));

  enableIfValid();
}

void 
VisModel::onApply()
{
  GxyModel::onApply();
}

unsigned int
VisModel::nPorts(PortType portType) const
{
  return 1; // PortType::In or ::Out
}

void
VisModel::onDataRangeReset()
{
  const QSignalBlocker b(this);

  cmap_range_min_slider->setSliderPosition(0);
  cmap_range_min->setText(QString::number(data_minimum));

  cmap_range_max_slider->setSliderPosition(1000);
  cmap_range_max->setText(QString::number(data_maximum));
}

QtNodes::NodeValidationState
VisModel::validationState() const
{
  return NodeValidationState::Valid;
}

QString
VisModel::validationMessage() const
{
  return QString("copacetic");
}

QJsonObject
VisModel::save() const 
{
  QJsonObject modelJson = GxyModel::save();
  output->toJson(modelJson);
  return modelJson;
}

bool 
VisModel::isValid() 
{
  bool valid = true;
  return (GxyModel::isValid() && input && input->isValid());
}

void
VisModel::loadInputDrivenWidgets(std::shared_ptr<GxyPacket> p) 
{
  if (p && p->isValid())
  {
    std::shared_ptr<GxyData> d = std::dynamic_pointer_cast<GxyData>(p);

    data_minimum = d->dataInfo.data_min;
    data_maximum = d->dataInfo.data_max;

    if (! data_range_set)
    {
      onDataRangeReset();
      data_range_set = true;
    }

    GxyModel::loadInputDrivenWidgets(p);
  }
}

void
VisModel::loadParameterWidgets() const
{
  if (output)
  {
    GxyModel::loadParameterWidgets();

    std::shared_ptr<Vis> v = std::dynamic_pointer_cast<Vis>(output);

    cmap_widget->setText(v->colormap_file.c_str());
    cmap_range_min->setText(QString::number(v->cmap_range_min));
    cmap_range_max->setText(QString::number(v->cmap_range_max));
  }
}

void
VisModel::loadOutput(std::shared_ptr<GxyData> p) const
{
  GxyModel::loadOutput(p);

  std::shared_ptr<Vis> v = std::dynamic_pointer_cast<Vis>(p);

  v->source = input->dataInfo.name.c_str();
  v->colormap_file = cmap_widget->text().toStdString();
  v->cmap_range_min = cmap_range_min->text().toDouble();
  v->cmap_range_max = cmap_range_max->text().toDouble();

  return;
}

void
VisModel::onCmapMinTextChanged()
{
  const QSignalBlocker b(this);

  float value = cmap_range_min->text().toDouble();
  cmap_range_min_slider->setSliderPosition(int(((value - data_minimum) / (data_maximum - data_minimum)) * 1000));

  if (cmap_range_max_slider->sliderPosition() < cmap_range_min_slider->sliderPosition())
  {
    cmap_range_max->setText(QString::number(value));
    cmap_range_max_slider->setSliderPosition(cmap_range_min_slider->sliderPosition());
  }
}
    
void
VisModel::onCmapMaxTextChanged()
{
  const QSignalBlocker b(this);

  float value = cmap_range_max->text().toDouble();
  cmap_range_max_slider->setSliderPosition(int(((value - data_minimum) / (data_maximum - data_minimum)) * 1000));

  if (cmap_range_max_slider->sliderPosition() < cmap_range_max_slider->sliderPosition())
  {
    cmap_range_min->setText(QString::number(value));
    cmap_range_min_slider->setSliderPosition(cmap_range_max_slider->sliderPosition());
  }
}
    
void
VisModel::onCmapMinSliderChanged(int v)
{
  const QSignalBlocker b(this);

  float value = data_minimum + ((cmap_range_min_slider->sliderPosition() / 1000.0) * (data_maximum - data_minimum));
  cmap_range_min->setText(QString::number(value));

  if (cmap_range_max_slider->sliderPosition() < cmap_range_min_slider->sliderPosition())
  {
    cmap_range_max->setText(QString::number(value));
    cmap_range_max_slider->setSliderPosition(cmap_range_min_slider->sliderPosition());
  }
}

void
VisModel::onCmapMaxSliderChanged(int v)
{
  const QSignalBlocker b(this);

  float value = data_minimum + ((cmap_range_max_slider->sliderPosition() / 1000.0) * (data_maximum - data_minimum));
  cmap_range_max->setText(QString::number(value));

  if (cmap_range_max_slider->sliderPosition() < cmap_range_min_slider->sliderPosition())
  {
    cmap_range_min->setText(QString::number(value));
    cmap_range_min_slider->setSliderPosition(cmap_range_max_slider->sliderPosition());
  }
}

void
VisModel::
setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex portIndex)
{
  input = std::dynamic_pointer_cast<GxyData>(data);
  if (input && output)
    output->dataInfo = input->dataInfo;
}


