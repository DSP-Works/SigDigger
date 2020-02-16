//
//    PanoramicDialog.cpp: Description
//    Copyright (C) 2020 Gonzalo José Carracedo Carballal
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as
//    published by the Free Software Foundation, either version 3 of the
//    License, or (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful, but
//    WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this program.  If not, see
//    <http://www.gnu.org/licenses/>
//

#include <PanoramicDialog.h>
#include <Suscan/Library.h>
#include "ui_PanoramicDialog.h"
#include "DefaultGradient.h"
#include "MainSpectrum.h"
#include <fstream>
#include <iomanip>
#include <limits>
#include <QFileDialog>
#include <QMessageBox>

using namespace SigDigger;

void
SavedSpectrum::set(qint64 start, qint64 end, const float *data, size_t size)
{
  this->start = start;
  this->end   = end;
  this->data.assign(data, data + size);
}

bool
SavedSpectrum::exportToFile(QString const &path)
{
  std::ofstream of(path.toStdString().c_str(), std::ofstream::binary);

  if (!of.is_open())
    return false;

  of << "%\n";
  of << "% Panoramic Spectrum file generated by SigDigger\n";
  of << "%\n\n";

  of << "freqMin = " << this->start << ";\n";
  of << "freqMax = " << this->end << ";\n";
  of << "PSD = [ ";

  of << std::setprecision(std::numeric_limits<float>::digits10);

  for (auto p : this->data)
    of << p << " ";

  of << "];\n";

  return true;
}

////////////////////////// PanoramicDialogConfig ///////////////////////////////
#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), this->field)
#define LOAD(field) this->field = conf.get(STRINGFY(field), this->field)

void
PanoramicDialogConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(fullRange);
  LOAD(rangeMin);
  LOAD(rangeMax);
  LOAD(panRangeMin);
  LOAD(panRangeMax);
  LOAD(lnbFreq);
  LOAD(device);
  LOAD(sampRate);
  LOAD(strategy);
  LOAD(partitioning);
  LOAD(palette);

  for (unsigned int i = 0; i < conf.getFieldCount(); ++i)
    if (conf.getFieldByIndex(i).name().substr(0, 5) == "gain.") {
      this->gains[conf.getFieldByIndex(i).name()] =
          conf.get(
            conf.getFieldByIndex(i).name(),
            static_cast<SUFLOAT>(0));
    }
}

Suscan::Object &&
PanoramicDialogConfig::serialize(void)
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("PanoramicDialogConfig");

  STORE(fullRange);
  STORE(rangeMin);
  STORE(rangeMax);
  STORE(panRangeMin);
  STORE(panRangeMax);
  STORE(lnbFreq);
  STORE(sampRate);
  STORE(device);
  STORE(strategy);
  STORE(partitioning);
  STORE(palette);

  for (auto p : this->gains)
    obj.set(p.first, p.second);

  return this->persist(obj);
}

bool
PanoramicDialogConfig::hasGain(
    std::string const &dev,
    std::string const &name) const
{
  std::string fullName = "gain." + dev + "." + name;

  return this->gains.find(fullName) != this->gains.cend();
}

SUFLOAT
PanoramicDialogConfig::getGain(
    std::string const &dev,
    std::string const &name) const
{
  std::string fullName = "gain." + dev + "." + name;

  if (this->gains.find(fullName) == this->gains.cend())
    return 0;

  return this->gains.at(fullName);
}

void
PanoramicDialogConfig::setGain(
    std::string const &dev,
    std::string const &name,
    SUFLOAT val)
{
  std::string fullName = "gain." + dev + "." + name;

  this->gains[fullName] = val;
}

///////////////////////////// PanoramicDialog //////////////////////////////////
PanoramicDialog::PanoramicDialog(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::PanoramicDialog)
{
  ui->setupUi(static_cast<QDialog *>(this));

  this->assertConfig();
  this->setWindowFlags(Qt::Window);
  this->connectAll();
}

PanoramicDialog::~PanoramicDialog()
{
  if (this->noGainLabel != nullptr)
    this->noGainLabel->deleteLater();
  delete ui;
}

void
PanoramicDialog::connectAll(void)
{
  connect(
        this->ui->deviceCombo,
        SIGNAL(activated(int)),
        this,
        SLOT(onDeviceChanged(void)));

  connect(
        this->ui->lnbDoubleSpinBox,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onLnbOffsetChanged(void)));

  connect(
        this->ui->sampleRateSpin,
        SIGNAL(valueChanged(int)),
        this,
        SLOT(onSampleRateSpinChanged(void)));

  connect(
        this->ui->fullRangeCheck,
        SIGNAL(stateChanged(int)),
        this,
        SLOT(onFullRangeChanged(void)));

  connect(
        this->ui->rangeStartSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onFreqRangeChanged(void)));

  connect(
        this->ui->rangeEndSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onFreqRangeChanged(void)));

  connect(
        this->ui->scanButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onToggleScan(void)));

  connect(
        this->ui->resetButton,
        SIGNAL(clicked(bool)),
        this,
        SIGNAL(reset(void)));

  connect(
        this->ui->waterfall,
        SIGNAL(newFilterFreq(int, int)),
        this,
        SLOT(onNewBandwidth(int, int)));

  connect(
        this->ui->waterfall,
        SIGNAL(newDemodFreq(qint64, qint64)),
        this,
        SLOT(onNewOffset()));

  connect(
        this->ui->waterfall,
        SIGNAL(newZoomLevel(float)),
        this,
        SLOT(onNewZoomLevel(void)));

  connect(
        this->ui->waterfall,
        SIGNAL(newCenterFreq(qint64)),
        this,
        SLOT(onNewCenterFreq(qint64)));

  connect(
        this->ui->rttSpin,
        SIGNAL(valueChanged(int)),
        this,
        SIGNAL(frameSkipChanged(void)));

  connect(
        this->ui->relBwSlider,
        SIGNAL(valueChanged(int)),
        this,
        SIGNAL(relBandwidthChanged(void)));

  connect(
        this->ui->waterfall,
        SIGNAL(pandapterRangeChanged(float, float)),
        this,
        SLOT(onRangeChanged(float, float)));

  connect(
        this->ui->paletteCombo,
        SIGNAL(activated(int)),
        this,
        SLOT(onPaletteChanged(int)));

  connect(
        this->ui->walkStrategyCombo,
        SIGNAL(currentIndexChanged(const QString &)),
        this,
        SLOT(onStrategyChanged(QString)));

  connect(
        this->ui->partitioningCombo,
        SIGNAL(currentIndexChanged(const QString &)),
        this,
        SIGNAL(partitioningChanged(QString)));

  connect(
        this->ui->exportButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onExport(void)));
}


// The following values are purely experimental
unsigned int
PanoramicDialog::preferredRttMs(Suscan::Source::Device const &dev)
{
  if (dev.getDriver() == "rtlsdr")
    return 60;
  else if (dev.getDriver() == "airspy")
    return 16;
  else if (dev.getDriver() == "hackrf")
    return 10;

  return 0;
}

void
PanoramicDialog::deserializePalettes(void)
{
  if (this->palettes.size() == 0) {
    Suscan::Singleton *sus = Suscan::Singleton::get_instance();
    int ndx = 0;
    this->palettes.push_back(Palette("Suscan", wf_gradient));
    this->palettes.push_back(*MainSpectrum::getGqrxPalette());

    for (auto i = sus->getFirstPalette();
         i != sus->getLastPalette();
         i++)
      this->palettes.push_back(Palette(*i));

    this->ui->paletteCombo->clear();

    // Populate combo
    for (auto p : this->palettes) {
      this->ui->paletteCombo->insertItem(
            ndx,
            QIcon(QPixmap::fromImage(p.getThumbnail())),
            QString::fromStdString(p.getName()),
            QVariant::fromValue(ndx));
      ++ndx;
    }

    this->setPaletteGradient(this->paletteGradient);
  }
}

void
PanoramicDialog::refreshUi(void)
{
  bool empty = this->deviceMap.size() == 0;
  bool fullRange = this->ui->fullRangeCheck->isChecked();

  this->ui->deviceCombo->setEnabled(!this->running && !empty);
  this->ui->fullRangeCheck->setEnabled(!this->running && !empty);
  this->ui->rangeEndSpin->setEnabled(!this->running && !empty && !fullRange);
  this->ui->rangeStartSpin->setEnabled(!this->running && !empty && !fullRange);
  this->ui->lnbDoubleSpinBox->setEnabled(!this->running);
  this->ui->scanButton->setChecked(this->running);
  this->ui->sampleRateSpin->setEnabled(!this->running);
}

SUFREQ
PanoramicDialog::getLnbOffset(void) const
{
  return this->ui->lnbDoubleSpinBox->value();
}

SUFREQ
PanoramicDialog::getMinFreq(void) const
{
  return this->ui->rangeStartSpin->value();
}

SUFREQ
PanoramicDialog::getMaxFreq(void) const
{
  return this->ui->rangeEndSpin->value();
}

void
PanoramicDialog::setRunning(bool running)
{
  if (running && !this->running) {
    this->frames = 0;
    this->ui->framesLabel->setText("0");
  } else if (!running && this->running) {
    this->ui->sampleRateSpin->setValue(this->dialogConfig->sampRate);
  }

  this->running = running;
  this->refreshUi();
}

QString
PanoramicDialog::getStrategy(void) const
{
  return this->ui->walkStrategyCombo->currentText();
}

QString
PanoramicDialog::getPartitioning(void) const
{
  if (this->getStrategy() == QString("Progressive"))
    return "Discrete";
  return this->ui->partitioningCombo->currentText();
}

float
PanoramicDialog::getGain(QString const &gain) const
{
  for (auto p : this->gainControls)
    if (p->getName() == gain.toStdString())
      return p->getGain();

  return 0;
}

void
PanoramicDialog::setBannedDevice(QString const &desc)
{
  this->bannedDevice = desc;
}

void
PanoramicDialog::setWfRange(qint64 freqStart, qint64 freqEnd)
{
  if (this->fixedFreqMode) {
    qint64 bw = static_cast<qint64>(this->minBwForZoom);

    // In fixed frequency mode we never set the center frequency.
    // That remains fixed. Spectrum is received according to the
    // waterfall's span.
    if (bw != this->currBw) {
      this->ui->waterfall->setSampleRate(bw);
      this->currBw = bw;
    }
  } else {
    quint64 fc = static_cast<quint64>(.5 * (freqEnd + freqStart));
    qint64 bw = static_cast<qint64>(freqEnd - freqStart);

    // In other cases, we must adjust the limits and the bandwidth.
    // When also have to adjust the bandwidth, we must reset the zoom
    // so the sure can keep zooming in the spectrum,

    this->ui->waterfall->setCenterFreq(fc);

    if (bw != this->currBw) {
      this->ui->waterfall->setLocked(false);
      this->ui->waterfall->setSampleRate(bw);
      this->ui->waterfall->setDemodRanges(
            -static_cast<int>(bw / 2),
            0,
            0,
            static_cast<int>(bw / 2),
            true);

      this->ui->waterfall->setHiLowCutFrequencies(
            -static_cast<int>(bw / 20),
            static_cast<int>(bw / 20));

      this->ui->waterfall->resetHorizontalZoom();
      this->currBw = bw;
    }
  }
}

void
PanoramicDialog::feed(
    quint64 freqStart,
    quint64 freqEnd,
    float *data,
    size_t size)
{
  if (this->freqStart != freqStart || this->freqEnd != freqEnd) {
    this->freqStart = freqStart;
    this->freqEnd   = freqEnd;

    this->adjustingRange = true;
    this->setWfRange(
          static_cast<qint64>(freqStart),
          static_cast<qint64>(freqEnd));
    this->adjustingRange = false;
  }

  this->saved.set(
        static_cast<qint64>(freqStart),
        static_cast<qint64>(freqEnd),
        data,
        size);

  this->ui->exportButton->setEnabled(true);
  this->ui->waterfall->setNewFftData(data, static_cast<int>(size));

  ++this->frames;
  this->redrawMeasures();
}

void
PanoramicDialog::setColors(ColorConfig const &cfg)
{
  this->ui->waterfall->setFftPlotColor(cfg.spectrumForeground);
  this->ui->waterfall->setFftAxesColor(cfg.spectrumAxes);
  this->ui->waterfall->setFftBgColor(cfg.spectrumBackground);
  this->ui->waterfall->setFftTextColor(cfg.spectrumText);
}

void
PanoramicDialog::setPaletteGradient(QString const &name)
{
  this->paletteGradient = name;

  for (unsigned i = 0; i < this->palettes.size(); ++i) {
    if (this->palettes[i].getName() == this->paletteGradient.toStdString()) {
      this->ui->paletteCombo->setCurrentIndex(
            static_cast<int>(i));
      this->ui->waterfall->setPalette(this->palettes[i].getGradient());
      break;
    }
  }
}

SUFLOAT
PanoramicDialog::getPreferredSampleRate(void) const
{
  return this->ui->sampleRateSpin->value();
}

void
PanoramicDialog::setMinBwForZoom(quint64 bw)
{
  this->minBwForZoom = bw;
  this->ui->sampleRateSpin->setValue(static_cast<int>(bw));
}

void
PanoramicDialog::populateDeviceCombo(void)
{
  Suscan::Singleton *sus = Suscan::Singleton::get_instance();

  this->ui->deviceCombo->clear();
  this->deviceMap.clear();

  for (auto i = sus->getFirstDevice(); i != sus->getLastDevice(); ++i) {
    if (i->getMaxFreq() > 0 && i->isAvailable()) {
      std::string name = i->getDesc();
      this->deviceMap[name] = *i;
      this->ui->deviceCombo->addItem(QString::fromStdString(name));
    }
  }

  if (this->deviceMap.size() > 0)
    this->onDeviceChanged();

  this->refreshUi();
}

bool
PanoramicDialog::getSelectedDevice(Suscan::Source::Device &dev) const
{
  std::string name = this->ui->deviceCombo->currentText().toStdString();
  auto p = this->deviceMap.find(name);

  if (p != this->deviceMap.cend()) {
    dev = p->second;
    return true;
  }

  return false;
}

void
PanoramicDialog::adjustRanges(void)
{
  if (this->ui->rangeStartSpin->value() >
      this->ui->rangeEndSpin->value()) {
    auto val = this->ui->rangeStartSpin->value();
    this->ui->rangeStartSpin->setValue(
          this->ui->rangeEndSpin->value());
    this->ui->rangeEndSpin->setValue(val);
  }
}

bool
PanoramicDialog::invalidRange(void) const
{
  return fabs(
        this->ui->rangeEndSpin->value() - this->ui->rangeStartSpin->value()) < 1;
}
void
PanoramicDialog::setRanges(Suscan::Source::Device const &dev)
{
  SUFREQ minFreq = dev.getMinFreq() - this->getLnbOffset();
  SUFREQ maxFreq = dev.getMaxFreq() - this->getLnbOffset();

  // Prevents Waterfall frequencies from overflowing.

  if (minFreq < 0)
    minFreq = 0;

  if (maxFreq < 0)
    maxFreq = 0;

  if (minFreq > 2e9)
    minFreq = 2e9;

  if (maxFreq > 2e9)
    maxFreq = 2e9;

  this->ui->rangeStartSpin->setMinimum(minFreq);
  this->ui->rangeStartSpin->setMaximum(maxFreq);
  this->ui->rangeEndSpin->setMinimum(minFreq);
  this->ui->rangeEndSpin->setMaximum(maxFreq);

  if (this->invalidRange() || this->ui->fullRangeCheck->isChecked()) {
    this->ui->rangeStartSpin->setValue(minFreq);
    this->ui->rangeEndSpin->setValue(maxFreq);
  }

  this->adjustRanges();
}

void
PanoramicDialog::saveConfig(void)
{
  Suscan::Source::Device dev;

  this->getSelectedDevice(dev);

  this->dialogConfig->device = dev.getDesc();
  this->dialogConfig->lnbFreq = this->ui->lnbDoubleSpinBox->value();
  this->dialogConfig->palette = this->paletteGradient.toStdString();
  this->dialogConfig->rangeMin = this->ui->rangeStartSpin->value();
  this->dialogConfig->rangeMax = this->ui->rangeEndSpin->value();

  this->dialogConfig->strategy =
      this->ui->walkStrategyCombo->currentText().toStdString();

  this->dialogConfig->partitioning =
      this->ui->partitioningCombo->currentText().toStdString();

  this->dialogConfig->fullRange = this->ui->fullRangeCheck->isChecked();
}

void
PanoramicDialog::run(void)
{
  this->populateDeviceCombo();
  this->deserializePalettes();
  this->exec();
  this->saveConfig();
  emit stop();
}

void
PanoramicDialog::redrawMeasures(void)
{
  this->demodFreq = static_cast<qint64>(
        this->ui->waterfall->getFilterOffset() +
        .5 * (this->freqStart + this->freqEnd));

  this->ui->centerLabel->setText(
        QString::number(
          static_cast<qint64>(
            this->ui->waterfall->getFilterOffset() +
            .5 * (this->freqStart + this->freqEnd))) + " Hz");

  this->ui->bwLabel->setText(
        QString::number(this->ui->waterfall->getFilterBw()) + " Hz");

  this->ui->framesLabel->setText(QString::number(this->frames));
}

unsigned int
PanoramicDialog::getRttMs(void) const
{
  return static_cast<unsigned int>(this->ui->rttSpin->value());
}

float
PanoramicDialog::getRelBw(void) const
{
  return this->ui->relBwSlider->value() / 100.f;
}

DeviceGain *
PanoramicDialog::lookupGain(std::string const &name)
{
  // Why is this? Use a map instead.
  for (auto p = this->gainControls.begin();
       p != this->gainControls.end();
       ++p) {
    if ((*p)->getName() == name)
      return *p;
  }

  return nullptr;
}

void
PanoramicDialog::clearGains(void)
{
  int i, len;

  len = static_cast<int>(this->gainControls.size());

  if (len == 0) {
    QLayoutItem *item = this->ui->gainGridLayout->takeAt(0);
    delete item;

    if (this->noGainLabel != nullptr) {
      this->noGainLabel->deleteLater();
      this->noGainLabel = nullptr;
    }
  } else {
    for (i = 0; i < len; ++i) {
      QLayoutItem *item = this->ui->gainGridLayout->takeAt(0);
      if (item != nullptr)
        delete item;

      // This is what C++ is for.
      this->gainControls[static_cast<unsigned long>(i)]->setVisible(false);
      this->gainControls[static_cast<unsigned long>(i)]->deleteLater();
    }

    QLayoutItem *item = this->ui->gainGridLayout->takeAt(0);
    if (item != nullptr)
      delete item;

    this->gainControls.clear();
  }
}

void
PanoramicDialog::refreshGains(Suscan::Source::Device &device)
{
  DeviceGain *gain = nullptr;

  this->clearGains();

  for (auto p = device.getFirstGain();
       p != device.getLastGain();
       ++p) {
    gain = new DeviceGain(nullptr, *p);
    this->gainControls.push_back(gain);
    this->ui->gainGridLayout->addWidget(
          gain,
          static_cast<int>(this->gainControls.size() - 1),
          0,
          1,
          1);

    connect(
          gain,
          SIGNAL(gainChanged(QString, float)),
          this,
          SLOT(onGainChanged(QString, float)));

    if (this->dialogConfig->hasGain(device.getDriver(), p->getName()))
      gain->setGain(this->dialogConfig->getGain(device.getDriver(), p->getName()));
    else
      gain->setGain(p->getDefault());
  }

  if (this->gainControls.size() == 0) {
    this->ui->gainGridLayout->addWidget(
        this->noGainLabel = new QLabel("(device has no gains)"),
        0,
        0,
        Qt::AlignCenter | Qt::AlignVCenter);
  } else {
    this->ui->gainGridLayout->addItem(
          new QSpacerItem(40, 20, QSizePolicy::Minimum, QSizePolicy::Minimum),
          static_cast<int>(this->gainControls.size()),
          0);
  }
}

// Overriden methods
Suscan::Serializable *
PanoramicDialog::allocConfig(void)
{
  return this->dialogConfig = new PanoramicDialogConfig();
}

void
PanoramicDialog::applyConfig(void)
{
  this->deserializePalettes();

  this->setPaletteGradient(QString::fromStdString(this->dialogConfig->palette));
  this->ui->lnbDoubleSpinBox->setValue(
        static_cast<SUFREQ>(this->dialogConfig->lnbFreq));
  this->ui->rangeStartSpin->setValue(this->dialogConfig->rangeMin);
  this->ui->rangeEndSpin->setValue(this->dialogConfig->rangeMax);
  this->ui->fullRangeCheck->setChecked(this->dialogConfig->fullRange);
  this->ui->sampleRateSpin->setValue(this->dialogConfig->sampRate);
  this->ui->waterfall->setPandapterRange(
        this->dialogConfig->panRangeMin,
        this->dialogConfig->panRangeMax);
  this->ui->waterfall->setWaterfallRange(
        this->dialogConfig->panRangeMin,
        this->dialogConfig->panRangeMax);
  this->onDeviceChanged();
}

////////////////////////////// Slots //////////////////////////////////////

void
PanoramicDialog::onDeviceChanged(void)
{
  Suscan::Source::Device dev;

  if (this->getSelectedDevice(dev)) {
    unsigned int rtt = preferredRttMs(dev);
    this->setRanges(dev);
    this->refreshGains(dev);
    if (rtt != 0)
      this->ui->rttSpin->setValue(static_cast<int>(rtt));
    if (this->ui->fullRangeCheck->isChecked()) {
      this->ui->rangeStartSpin->setValue(dev.getMinFreq());
      this->ui->rangeEndSpin->setValue(dev.getMaxFreq());
    }
  } else {
    this->clearGains();
  }
}

void
PanoramicDialog::onFullRangeChanged(void)
{
  Suscan::Source::Device dev;
  bool checked = this->ui->fullRangeCheck->isChecked();

  if (this->getSelectedDevice(dev)) {
    if (checked) {
      this->ui->rangeStartSpin->setValue(dev.getMinFreq());
      this->ui->rangeEndSpin->setValue(dev.getMaxFreq());
    }
  }

  this->refreshUi();
}

void
PanoramicDialog::onFreqRangeChanged(void)
{
  this->adjustRanges();
}

void
PanoramicDialog::onToggleScan(void)
{
  if (this->ui->scanButton->isChecked()) {
    Suscan::Source::Device dev;
    this->getSelectedDevice(dev);

    if (this->bannedDevice.length() > 0
        && dev.getDesc() == this->bannedDevice.toStdString()) {
      (void)  QMessageBox::critical(
            this,
            "Panoramic spectrum error error",
            "Scan cannot start because the selected device is in use by the main window.",
            QMessageBox::Ok);
      this->ui->scanButton->setChecked(false);
    } else {
      emit start();
    }
  } else {
    emit stop();
  }

  this->ui->scanButton->setText(
        this->ui->scanButton->isChecked()
        ? "Stop"
        : "Start scan");

}

void
PanoramicDialog::onNewZoomLevel(void)
{
  qint64 min, max;
  qint64 fc = abs(
        this->ui->waterfall->getCenterFreq()
        + this->ui->waterfall->getFftCenterFreq());
  qint64 span = static_cast<qint64>(this->ui->waterfall->getSpanFreq());
  bool adjLeft = false;
  bool adjRight = false;

  if (!this->adjustingRange) {
    this->adjustingRange = true;

    min = fc - span / 2;
    max = fc + span / 2;

    if (min < this->getMinFreq()) {
      min = static_cast<qint64>(this->getMinFreq());
      adjLeft = true;
    }

    if (max > this->getMaxFreq()) {
      max = static_cast<qint64>(this->getMaxFreq());
      adjRight = true;
    }

    if (adjLeft && adjRight)
      this->ui->waterfall->resetHorizontalZoom();

    this->fixedFreqMode = max - min <= this->minBwForZoom * this->getRelBw();

    if (this->fixedFreqMode) {
      fc = this->ui->waterfall->getCenterFreq();
      min = fc - span / 2;
      max = fc + span / 2;
    }

    this->setWfRange(min, max);
    this->adjustingRange = false;

    emit detailChanged(
          static_cast<quint64>(min),
          static_cast<quint64>(max),
          this->fixedFreqMode);
  }
}

void
PanoramicDialog::onRangeChanged(float min, float max)
{
  this->dialogConfig->panRangeMin = min;
  this->dialogConfig->panRangeMax = max;
  this->ui->waterfall->setWaterfallRange(min, max);
}

void
PanoramicDialog::onNewOffset(void)
{
  this->redrawMeasures();
}

void
PanoramicDialog::onNewBandwidth(int, int)
{
  this->redrawMeasures();
}

void
PanoramicDialog::onNewCenterFreq(qint64 freq)
{
  qint64 span = this->currBw;
  qint64 min = freq - span / 2;
  qint64 max = freq + span / 2;
  bool smallRange;
  bool leftBorder = false;
  bool rightBorder = false;
  if (min <= this->getMinFreq()) {
    leftBorder = true;
    min = static_cast<qint64>(this->getMinFreq());
  }

  if (max >= this->getMaxFreq()) {
    rightBorder = true;
    max = static_cast<qint64>(this->getMaxFreq());
  }

  smallRange = static_cast<quint64>(max - min) <= this->minBwForZoom;

  if (smallRange) {
    if (leftBorder && !rightBorder) {
      max = min + span;
    } else if (rightBorder && !leftBorder) {
      min = max - span;
    }
  }

  if (rightBorder || leftBorder)
    this->ui->waterfall->setCenterFreq(
        static_cast<quint64>(.5 * (max + min)));

  emit detailChanged(
        static_cast<quint64>(min),
        static_cast<quint64>(max),
        this->fixedFreqMode);
}

void
PanoramicDialog::onPaletteChanged(int)
{
  this->setPaletteGradient(this->ui->paletteCombo->currentText());
}

void
PanoramicDialog::onStrategyChanged(QString strategy)
{
  this->ui->partitioningCombo->setEnabled(strategy != QString("Progressive"));
  emit strategyChanged(strategy);
}

void
PanoramicDialog::onLnbOffsetChanged(void)
{
  Suscan::Source::Device dev;

  if (this->getSelectedDevice(dev))
    this->setRanges(dev);
}

void
PanoramicDialog::onExport(void)
{
  bool done = false;

  do {
    QFileDialog dialog(this);

    dialog.setFileMode(QFileDialog::FileMode::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setWindowTitle(QString("Save panoramic spectrum"));
    dialog.setNameFilter(QString("MATLAB/Octave file (*.m)"));

    if (dialog.exec()) {
      QString path = dialog.selectedFiles().first();

        if (!this->saved.exportToFile(path)) {
          QMessageBox::warning(
                this,
                "Cannot open file",
                "Cannote save file in the specified location. Please choose "
                "a different location and try again.",
                QMessageBox::Ok);
        } else {
          done = true;
        }
    } else {
      done = true;
    }
  } while (!done);
}

void
PanoramicDialog::onGainChanged(QString name, float val)
{
  Suscan::Source::Device dev;

  if (this->getSelectedDevice(dev))
    this->dialogConfig->setGain(dev.getDriver(), name.toStdString(), val);

  emit gainChanged(name, val);
}

void
PanoramicDialog::onSampleRateSpinChanged(void)
{
  if (!this->running)
    this->dialogConfig->sampRate = this->ui->sampleRateSpin->value();
}
