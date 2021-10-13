/*
 * Copyright (c) 2013-2014,2016-2017,2019-2021 - Adjacent Link LLC,
 * Bridgewater, New Jersey
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Adjacent Link LLC nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "monitorphy.h"

#include "emane/commonphyheader.h"
#include "emane/configureexception.h"
#include "emane/spectrumserviceexception.h"
#include "emane/startexception.h"

#include "emane/utils/conversionutils.h"

#include "emane/events/antennaprofileevent.h"
#include "emane/events/antennaprofileeventformatter.h"
#include "emane/events/locationevent.h"
#include "emane/events/locationeventformatter.h"
#include "emane/events/pathlossevent.h"
#include "emane/events/pathlosseventformatter.h"
#include "emane/events/fadingselectionevent.h"
#include "emane/events/fadingselectioneventformatter.h"

#include "emane/controls/otatransmittercontrolmessage.h"
#include "emane/controls/receivepropertiescontrolmessage.h"
#include "emane/controls/antennaprofilecontrolmessage.h"
#include "emane/controls/spectrumfilteraddcontrolmessage.h"
#include "emane/controls/spectrumfilterremovecontrolmessage.h"
#include "emane/controls/spectrumfilterdatacontrolmessage.h"
#include "emane/controls/mimoreceivepropertiescontrolmessage.h"

#include "emane/controls/transmittercontrolmessageformatter.h"
#include "emane/controls/frequencyofinterestcontrolmessageformatter.h"
#include "emane/controls/spectrumfilteraddcontrolmessageformatter.h"
#include "emane/controls/spectrumfilterremovecontrolmessageformatter.h"
#include "emane/controls/rxantennaupdatecontrolmessageformatter.h"
#include "emane/controls/rxantennaremovecontrolmessageformatter.h"
#include "emane/controls/rxantennaaddcontrolmessageformatter.h"

#include "freespacepropagationmodelalgorithm.h"
#include "tworaypropagationmodelalgorithm.h"
#include "precomputedpropagationmodelalgorithm.h"
#include "spectrumservice.h"

#include "spectrummonitor.pb.h"
#include "maxnoisebin.h"

#include <iterator>
#include <zmq.h>
#include <algorithm>
#include <iterator>

namespace
{
  const std::uint16_t DROP_CODE_OUT_OF_BAND                 = 1;
  const std::uint16_t DROP_CODE_RX_SENSITIVITY              = 2;
  const std::uint16_t DROP_CODE_PROPAGATIONMODEL            = 3;
  const std::uint16_t DROP_CODE_GAINMANAGER_LOCATION        = 4;
  const std::uint16_t DROP_CODE_GAINMANAGER_HORIZON         = 5;
  const std::uint16_t DROP_CODE_GAINMANAGER_ANTENNAPROFILE  = 6;
  const std::uint16_t DROP_CODE_NOT_FOI                     = 7;
  const std::uint16_t DROP_CODE_SPECTRUM_CLAMP              = 8;
  const std::uint16_t DROP_CODE_FADINGMANAGER_LOCATION      = 9;
  const std::uint16_t DROP_CODE_FADINGMANAGER_ALGORITHM     = 10;
  const std::uint16_t DROP_CODE_FADINGMANAGER_SELECTION     = 11;
  const std::uint16_t DROP_CODE_ANTENNA_FREQ_INDEX          = 12;
  const std::uint16_t DROP_CODE_GAINMANAGER_ANTENNA_INDEX   = 13;
  const std::uint16_t DROP_CODE_MISSING_CONTROL             = 14;

  EMANE::StatisticTableLabels STATISTIC_TABLE_LABELS{"Out-of-Band",
    "Rx Sensitivity",
    "Propagation Model",
    "Gain Location",
    "Gain Horizon",
    "Gain Profile",
    "Not FOI",
    "Spectrum Clamp",
    "Fade Location",
    "Fade Algorithm",
    "Fade Select",
    "Antenna Freq",
    "Gain Antenna",
    "Missing Control"};

  const std::string FADINGMANAGER_PREFIX{"fading."};
}

EMANE::SpectrumTools::MonitorPhy::MonitorPhy(NEMId id,
                                             PlatformServiceProvider * pPlatformService):
  PHYLayerImplementor{id, pPlatformService},
  pSpectrumService_{new SpectrumService{}},
  antennaManager_{},
  locationManager_{id},
  u64BandwidthHz_{},
  u64RxCenterFrequencyHz_{},
  u64SubbandBinSizeHz_{},
  commonLayerStatistics_{STATISTIC_TABLE_LABELS,{},"0"},
  eventTablePublisher_{id},
  noiseBinSize_{},
  maxSegmentOffset_{},
  maxMessagePropagation_{},
  maxSegmentDuration_{},
  timeSyncThreshold_{},
  bNoiseMaxClamp_{},
  dSystemNoiseFiguredB_{},
  pTimeSyncThresholdRewrite_{},
  pGainCacheHit_{},
  pGainCacheMiss_{},
  fadingManager_{id, pPlatformService,FADINGMANAGER_PREFIX}{}


EMANE::SpectrumTools::MonitorPhy::~MonitorPhy(){}

void EMANE::SpectrumTools::MonitorPhy::initialize(Registrar & registrar)
{
  auto & configRegistrar = registrar.configurationRegistrar();

  configRegistrar.registerNumeric<double>("fixedantennagain",
                                          EMANE::ConfigurationProperties::DEFAULT |
                                          EMANE::ConfigurationProperties::MODIFIABLE,
                                          {0},
                                          "Defines the antenna gain in dBi and is valid only when"
                                          " fixedantennagainenable is enabled.");

  configRegistrar.registerNumeric<bool>("fixedantennagainenable",
                                        EMANE::ConfigurationProperties::DEFAULT,
                                        {true},
                                        "Defines whether fixed antenna gain is used or whether"
                                        " antenna profiles are in use.");

  configRegistrar.registerNumeric<std::uint64_t>("noisebinsize",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {10000},
                                                 "Defines the noise bin size in microseconds and translates"
                                                 " into timing accuracy associated with aligning the start and"
                                                 " end of reception times of multiple packets for modeling of"
                                                 " interference effects.",
                                                 1);

  configRegistrar.registerNumeric<bool>("noisemaxclampenable",
                                        EMANE::ConfigurationProperties::DEFAULT,
                                        {false},
                                        "Defines whether segment offset, segment duration and message"
                                        " propagation associated with a received packet will be clamped"
                                        " to their respective maximums defined by noisemaxsegmentoffset,"
                                        " noisemaxsegmentduration and noisemaxmessagepropagation. When"
                                        " disabled, any packet with an above max value will be dropped.");


  configRegistrar.registerNumeric<std::uint64_t>("noisemaxsegmentoffset",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {300000},
                                                 "Noise maximum segment offset in microseconds.",
                                                 1);

  configRegistrar.registerNumeric<std::uint64_t>("noisemaxmessagepropagation",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {200000},
                                                 "Noise maximum message propagation in microseconds.",
                                                 1);

  configRegistrar.registerNumeric<std::uint64_t>("noisemaxsegmentduration",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {1000000},
                                                 "Noise maximum segment duration in microseconds.",
                                                 1);

  configRegistrar.registerNumeric<std::uint64_t>("timesyncthreshold",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {10000},
                                                 "Defines the time sync detection threshold in microseconds."
                                                 " If a received OTA message is more than this threshold, the"
                                                 " message reception time will be used as the source transmission"
                                                 " time instead of the time contained in the Common Phy Header."
                                                 " This allows the emulator to be used across distributed nodes"
                                                 " without time sync.",
                                                 1);

  configRegistrar.registerNonNumeric<std::string>("propagationmodel",
                                                  EMANE::ConfigurationProperties::DEFAULT,
                                                  {"precomputed"},
                                                  "Defines the pathloss mode of operation:"
                                                  " precomputed, 2ray or freespace.",
                                                  1,
                                                  1,
                                                  "^(precomputed|2ray|freespace)$");

  configRegistrar.registerNumeric<std::uint64_t>("spectrumquery.rate",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {100000},
                                                 "Defines the rate in microseconds to query the spectrum monitor"
                                                 " for max signal energy. Must be evenly divisable by the noisebinsize.",
                                                 1);

  configRegistrar.registerNumeric<std::uint64_t>("spectrumquery.binsize",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {10000},
                                                 "Defines the bin size to for calculating max signal enegty in a"
                                                 " single spectrum query. Must be evenly divisable by the"
                                                 " noisebinsize and <= spectrumquery.rate.",
                                                 1);

  configRegistrar.registerNonNumeric<INETAddr>("spectrumquery.publishendpoint",
                                               ConfigurationProperties::DEFAULT,
                                               {INETAddr{"0.0.0.0",8883}},
                                               "Spectrum Query ZMQ Pub socket endpoint.");

  configRegistrar.registerNumeric<bool>("stats.receivepowertableenable",
                                        EMANE::ConfigurationProperties::DEFAULT,
                                        {true},
                                        "Defines whether the receive power table will be populated. Large number"
                                        " of antenna (MIMO) and/or frequency segments will increases processing"
                                        " load when populating.");


  auto & eventRegistrar = registrar.eventRegistrar();

  eventRegistrar.registerEvent(Events::PathlossEvent::IDENTIFIER);

  eventRegistrar.registerEvent(Events::LocationEvent::IDENTIFIER);

  eventRegistrar.registerEvent(Events::AntennaProfileEvent::IDENTIFIER);

  eventRegistrar.registerEvent(Events::FadingSelectionEvent::IDENTIFIER);

  auto & statisticRegistrar = registrar.statisticRegistrar();

  commonLayerStatistics_.registerStatistics(statisticRegistrar);

  eventTablePublisher_.registerStatistics(statisticRegistrar);

  receivePowerTablePublisher_.registerStatistics(statisticRegistrar);

  pTimeSyncThresholdRewrite_ =
    statisticRegistrar.registerNumeric<std::uint64_t>("numTimeSyncThresholdRewrite",
                                                      StatisticProperties::CLEARABLE);
  pGainCacheHit_ =
    statisticRegistrar.registerNumeric<std::uint64_t>("numGainCacheHit",
                                                      StatisticProperties::CLEARABLE);

  pGainCacheMiss_ =
    statisticRegistrar.registerNumeric<std::uint64_t>("numGainCacheMiss",
                                                      StatisticProperties::CLEARABLE);

  fadingManager_.initialize(registrar);
}

void EMANE::SpectrumTools::MonitorPhy::configure(const ConfigurationUpdate & update)
{
  ConfigurationUpdate fadingManagerConfiguration{};

  for(const auto & item : update)
    {
      if(item.first == "subbandbinsize")
        {
          u64SubbandBinSizeHz_ = item.second[0].asUINT64();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %ju Hz",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  u64SubbandBinSizeHz_);
        }
      else if(item.first == "fixedantennagain")
        {
          optionalFixedAntennaGaindBi_.first = item.second[0].asDouble();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %3.2f dBi",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  optionalFixedAntennaGaindBi_.first);

        }
      else if(item.first == "fixedantennagainenable")
        {
          optionalFixedAntennaGaindBi_.second = item.second[0].asBool();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %s",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  optionalFixedAntennaGaindBi_.second ? "on" : "off");
        }
      else if(item.first == "propagationmodel")
        {
          std::string sPropagationModel{item.second[0].asString()};

          // regex has already validated values
          if(sPropagationModel == "precomputed")
            {
              pPropagationModelAlgorithm_.reset(new PrecomputedPropagationModelAlgorithm{id_});
            }
          else if(sPropagationModel == "2ray")
            {
              pPropagationModelAlgorithm_.reset(new TwoRayPropagationModelAlgorithm{id_});
            }
          else
            {
              pPropagationModelAlgorithm_.reset(new FreeSpacePropagationModelAlgorithm{id_});
            }

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %s",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  sPropagationModel.c_str());
        }
      else if(item.first == "noisebinsize")
        {
          noiseBinSize_ = Microseconds{item.second[0].asUINT64()};

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %ju usec",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  noiseBinSize_.count());
        }
      else if(item.first == "noisemaxclampenable")
        {
          bNoiseMaxClamp_ = item.second[0].asBool();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %s",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  bNoiseMaxClamp_ ? "on" : "off");
        }
      else if(item.first == "noisemaxsegmentoffset")
        {
          maxSegmentOffset_ = Microseconds{item.second[0].asUINT64()};

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %ju usec",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  maxSegmentOffset_.count());
        }
      else if(item.first == "noisemaxmessagepropagation")
        {
          maxMessagePropagation_ = Microseconds{item.second[0].asUINT64()};

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %ju usec",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  maxMessagePropagation_.count());
        }
      else if(item.first == "noisemaxsegmentduration")
        {
          maxSegmentDuration_ = Microseconds{item.second[0].asUINT64()};

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %ju usec",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  maxSegmentDuration_.count());
        }
      else if(item.first == "timesyncthreshold")
        {
          timeSyncThreshold_ = Microseconds{item.second[0].asUINT64()};

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %ju usec",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  timeSyncThreshold_.count());
        }
      else if(item.first == "systemnoisefigure")
        {
          dSystemNoiseFiguredB_ = item.second[0].asDouble();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %3.2f dB",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  dSystemNoiseFiguredB_);
        }
      else if(item.first == "spectrumquery.rate")
        {
          spectrumQueryRate_ = Microseconds{item.second[0].asUINT64()};

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %ju usec",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  spectrumQueryRate_.count());
        }
      else if(item.first == "spectrumquery.binsize")
        {
          spectrumQueryBinSize_ = Microseconds{item.second[0].asUINT64()};

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %ju usec",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  spectrumQueryBinSize_.count());
        }
      else if(item.first == "spectrumquery.publishendpoint")
        {
          spectrumQueryPublishAddr_ = item.second[0].asINETAddr();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %s",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  spectrumQueryPublishAddr_.str().c_str());
        }
      else if(item.first == "spectrumquery.recorderfile")
        {
          sSpectrumQueryRecorderFile_ = item.second[0].asString();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %s",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  sSpectrumQueryRecorderFile_.c_str());
        }
      else if(item.first == "stats.receivepowertableenable")
        {
          bStatsReceivePowerTableEnable_ = item.second[0].asBool();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %s",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  bStatsReceivePowerTableEnable_ ? "on" : "off");
        }
      else
        {
          if(!item.first.compare(0,FADINGMANAGER_PREFIX.size(),FADINGMANAGER_PREFIX))
            {
              fadingManagerConfiguration.push_back(item);
            }
          else
            {
              throw makeException<ConfigureException>("MonitorPhy: Unexpected configuration item %s",
                                                      item.first.c_str());
            }
        }
    }

  fadingManager_.configure(fadingManagerConfiguration);

  if((maxSegmentOffset_ + maxMessagePropagation_ + 2 * maxSegmentDuration_) % noiseBinSize_ !=
     Microseconds::zero())
    {
      throw makeException<ConfigureException>("noisemaxsegmentoffset + noisemaxmessagepropagation"
                                              " + 2 * noisemaxsegmentduration not evenly divisible by the"
                                              " noisebinsize");
    }

  pSpectrumService_->initialize(0, //subid
                                NoiseMode::OUTOFBAND,
                                noiseBinSize_,
                                maxSegmentOffset_,
                                maxMessagePropagation_,
                                maxSegmentDuration_,
                                timeSyncThreshold_,
                                bNoiseMaxClamp_,
                                false);

}

void EMANE::SpectrumTools::MonitorPhy::start()
{
  LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                          DEBUG_LEVEL,
                          "PHYI %03hu MonitorPhy::%s",
                          id_,
                          __func__);

  // add default antenna
  Antenna rxAntenna = optionalFixedAntennaGaindBi_.second ?
    Antenna::createIdealOmni(DEFAULT_ANTENNA_INDEX,
                             optionalFixedAntennaGaindBi_.first) :
    Antenna::createProfileDefined(DEFAULT_ANTENNA_INDEX);

  antennaManager_.update(id_,rxAntenna);

  pZMQContext_ = zmq_ctx_new();

  if(!pZMQContext_)
    {
      throw makeException<StartException>("unable to create zmq context");
    }


  pZMQSocket_ = zmq_socket(pZMQContext_,ZMQ_PUB);

  if(!pZMQSocket_)
    {
      throw makeException<StartException>("unable to create zmq pubsub socket");
    }

  std::string sPublishEndpoint{"tcp://"};

  sPublishEndpoint += spectrumQueryPublishAddr_.str();

  if(zmq_bind(pZMQSocket_,sPublishEndpoint.c_str()) < 0)
    {
      throw makeException<StartException>("Error binding publish socket: %s",
                                          zmq_strerror(errno));
    }


  if(!sSpectrumQueryRecorderFile_.empty())
    {
      recorderFileStream_.open(sSpectrumQueryRecorderFile_,
                               std::fstream::out | std::fstream::trunc);

      if(!recorderFileStream_)
        {
          throw makeException<StartException>("Unable to open: %s",
                                              sSpectrumQueryRecorderFile_.c_str());
        }
    }

  querySpectrumService();
}

void EMANE::SpectrumTools::MonitorPhy::stop()
{
  LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                          DEBUG_LEVEL,
                          "PHYI %03hu MonitorPhy::%s",
                          id_,
                          __func__);

}

void EMANE::SpectrumTools::MonitorPhy::destroy() throw()
{
  LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                          DEBUG_LEVEL,
                          "PHYI %03hu MonitorPhy::%s",
                          id_,
                          __func__);

}

void EMANE::SpectrumTools::MonitorPhy::processConfiguration(const ConfigurationUpdate & update)
{
  ConfigurationUpdate fadingManagerConfiguration{};

  for(const auto & item : update)
    {
      if(item.first == "fixedantennagain")
        {
          optionalFixedAntennaGaindBi_.first = item.second[0].asDouble();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu MonitorPhy::%s: %s = %3.2f dBi",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  optionalFixedAntennaGaindBi_.first);

        }
      else
        {
          if(!item.first.compare(0,FADINGMANAGER_PREFIX.size(),FADINGMANAGER_PREFIX))
            {
              fadingManagerConfiguration.push_back(item);
            }
        }
    }

  fadingManager_.modify(fadingManagerConfiguration);
}

void EMANE::SpectrumTools::MonitorPhy::processDownstreamControl(const ControlMessages &)
{}

void EMANE::SpectrumTools::MonitorPhy::processDownstreamPacket(DownstreamPacket &,
                                                               const ControlMessages &)
{}


void EMANE::SpectrumTools::MonitorPhy::processUpstreamPacket(const CommonPHYHeader & commonPhyHeader,
                                                             UpstreamPacket & pkt,
                                                             const ControlMessages &)
{
  processUpstreamPacket_i(Clock::now(),commonPhyHeader,pkt,{});
}

void EMANE::SpectrumTools::MonitorPhy::processUpstreamPacket_i(const TimePoint & now,
                                                               const CommonPHYHeader & commonPHYHeader,
                                                               UpstreamPacket & pkt,
                                                               const ControlMessages &)
{
  LOGGER_VERBOSE_LOGGING_FN_VARGS(pPlatformService_->logService(),
                                  DEBUG_LEVEL,
                                  std::bind(&CommonPHYHeader::format, std::ref(commonPHYHeader)),
                                  "PHYI %03hu MonitorPhy::%s Common Phy Header",
                                  id_,
                                  __func__);

  const  auto & pktInfo = pkt.getPacketInfo();

  commonLayerStatistics_.processInbound(pkt);

  if(!commonPHYHeader.getTransmitAntennas().size())
    {
      LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                              ERROR_LEVEL,
                              "PHYI %03hu MonitorPhy::%s "
                              " src %hu, dst %hu, drop no tx antennas",
                              id_,
                              __func__,
                              pktInfo.getSource(),
                              pktInfo.getDestination());



      commonLayerStatistics_.processOutbound(pkt,
                                             std::chrono::duration_cast<Microseconds>(Clock::now() - now),
                                             DROP_CODE_MISSING_CONTROL);

      // drop
      return;
    }

  auto iter  = spectrumMap_.find(commonPHYHeader.getSubId());

  if(iter == spectrumMap_.end())
    {
      auto pSpectrumMonitorAlt = new SpectrumMonitorAlt{commonPHYHeader.getSubId(),
        noiseBinSize_,
        maxSegmentOffset_,
        maxMessagePropagation_,
        maxSegmentDuration_,
        timeSyncThreshold_,
        bNoiseMaxClamp_};

      iter =
        spectrumMap_.insert(std::make_pair(commonPHYHeader.getSubId(),
                                           std::make_tuple(commonPHYHeader.getTransmitAntennas()[0].getBandwidthHz(),
                                                           std::unique_ptr<SpectrumMonitorAlt>(pSpectrumMonitorAlt),
                                                           std::unique_ptr<ReceiveProcessorAlt>(new ReceiveProcessorAlt{id_,
                                                                                                                        0,
                                                                                                                        DEFAULT_ANTENNA_INDEX,
                                                                                                                        antennaManager_,
                                                                                                                        pSpectrumMonitorAlt,
                                                                                                                        pPropagationModelAlgorithm_.get(),
                                                                                                                        fadingManager_.createFadingAlgorithmStore(),
                                                                                                                        bStatsReceivePowerTableEnable_})))).first;

    }
  else
    {
      if(std::get<0>(iter->second) != commonPHYHeader.getTransmitAntennas()[0].getBandwidthHz())
        {
          std::get<0>(iter->second) = commonPHYHeader.getTransmitAntennas()[0].getBandwidthHz();
        }
    }

  std::vector<std::pair<LocationInfo,bool>> locationInfos{};
  std::vector<std::pair<FadingInfo,bool>> fadingSelections{};

  for(const auto & transmitter : commonPHYHeader.getTransmitters())
    {
      locationInfos.push_back(locationManager_.getLocationInfo(transmitter.getNEMId()));

      fadingSelections.push_back(fadingManager_.getFadingSelection(transmitter.getNEMId()));

      for(const auto & txAntenna : commonPHYHeader.getTransmitAntennas())
        {
          antennaManager_.update(transmitter.getNEMId(),txAntenna);
        }
    }

  auto result = std::get<2>(iter->second)->process(now,
                                                   commonPHYHeader,
                                                   locationInfos,
                                                   fadingSelections,
                                                   false);

  if(result.status_ == ReceiveProcessorAlt::ProcessResult::Status::SUCCESS)
    {
      if(result.bGainCacheHit_)
        {
          ++*pGainCacheHit_;
        }
      else
        {
          ++*pGainCacheMiss_;
        }

      for(const auto & entry : result.receivePowerMap_)
        {
          receivePowerTablePublisher_.update(std::get<0>(entry.first),
                                             std::get<1>(entry.first),
                                             std::get<2>(entry.first),
                                             std::get<3>(entry.first),
                                             std::get<0>(entry.second),
                                             std::get<1>(entry.second),
                                             std::get<2>(entry.second),
                                             std::get<3>(entry.second),
                                             std::get<4>(entry.second),
                                             0,
                                             commonPHYHeader.getTxTime());
        }
    }
  else
    {
      std::string sReason{"unknown"};
      LogLevel logLevel{DEBUG_LEVEL};
      bool bNoError{};

      Microseconds processingDuration{std::chrono::duration_cast<Microseconds>(Clock::now() - now)};

      switch(result.status_)
        {
        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_ANTENNA_FREQ_INDEX:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_ANTENNA_FREQ_INDEX);
          sReason = "transmit antenna frequency index invalid";
          logLevel = ERROR_LEVEL;
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_FADINGMANAGER_LOCATION:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_FADINGMANAGER_LOCATION);
          sReason = "FadingManager missing location information";
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_FADINGMANAGER_ALGORITHM:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_FADINGMANAGER_ALGORITHM);

          sReason = "FadingManager unknown algorithm";
          logLevel = ERROR_LEVEL;
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_FADINGMANAGER_SELECTION:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_FADINGMANAGER_SELECTION);

          sReason = "FadingManager unknown fading selection for transmitter";

          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_GAINMANAGER_LOCATION:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_GAINMANAGER_LOCATION);

          sReason = "GainManager missing location information";
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_GAINMANAGER_ANTENNAPROFILE:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_GAINMANAGER_ANTENNAPROFILE);
          sReason = "GainManager unknown antenna profile";
          logLevel = ERROR_LEVEL;
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_GAINMANAGER_HORIZON:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_GAINMANAGER_HORIZON);
          sReason = "GainManager below horizon";
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_GAINMANAGER_ANTENNA_INDEX:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_GAINMANAGER_ANTENNA_INDEX);
          sReason = "GainManager unknown antenna index";
          logLevel = ERROR_LEVEL;
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_PROPAGATIONMODEL:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_PROPAGATIONMODEL);
          sReason = "propagation model missing information";
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_SPECTRUM_CLAMP:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_SPECTRUM_CLAMP);
          sReason = "SpectrumManager detected request range error";
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_NOT_FOI:
          commonLayerStatistics_.processOutbound(pkt,
                                                 processingDuration,
                                                 DROP_CODE_NOT_FOI);
          sReason = "message frequency not in frequency of interest list.";
          break;

        case ReceiveProcessorAlt::ProcessResult::Status::DROP_CODE_OUT_OF_BAND:
          bNoError = true;
          break;

        default:
          break;
        }

      if(!bNoError)
        {
          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  logLevel,
                                  "PHYI %03hu MonitorPhy::%s "
                                  " src %hu, dst %hu, drop %s",
                                  id_,
                                  __func__,
                                  pktInfo.getSource(),
                                  pktInfo.getDestination(),
                                  sReason.c_str());

          // drop
          return;
        }
    }
}


void EMANE::SpectrumTools::MonitorPhy::processEvent(const EventId & eventId,
                                                    const Serialization & serialization)
{
  LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                          DEBUG_LEVEL,
                          "PHYI %03hu MonitorPhy::%s event id: %hu",
                          id_,
                          __func__,
                          eventId);

  switch(eventId)
    {
    case Events::AntennaProfileEvent::IDENTIFIER:
      {
        Events::AntennaProfileEvent antennaProfile{serialization};
        antennaManager_.update(antennaProfile.getAntennaProfiles());
        eventTablePublisher_.update(antennaProfile.getAntennaProfiles());

        LOGGER_STANDARD_LOGGING_FN_VARGS(pPlatformService_->logService(),
                                         DEBUG_LEVEL,
                                         Events::AntennaProfileEventFormatter(antennaProfile),
                                         "PHYI %03hu MonitorPhy::%s antenna profile event: ",
                                         id_,
                                         __func__);

      }
      break;

    case Events::FadingSelectionEvent::IDENTIFIER:
      {
        Events::FadingSelectionEvent fadingSelection{serialization};
        fadingManager_.update(fadingSelection.getFadingSelections());
        eventTablePublisher_.update(fadingSelection.getFadingSelections());

        LOGGER_STANDARD_LOGGING_FN_VARGS(pPlatformService_->logService(),
                                         DEBUG_LEVEL,
                                         Events::FadingSelectionEventFormatter(fadingSelection),
                                         "PHYI %03hu MonitorPhy::%s fading selection event: ",
                                         id_,
                                         __func__);
      }
      break;

    case Events::LocationEvent::IDENTIFIER:
      {
        Events::LocationEvent locationEvent{serialization};
        locationManager_.update(locationEvent.getLocations());
        eventTablePublisher_.update(locationEvent.getLocations());

        LOGGER_STANDARD_LOGGING_FN_VARGS(pPlatformService_->logService(),
                                         DEBUG_LEVEL,
                                         Events::LocationEventFormatter(locationEvent),
                                         "PHYI %03hu MonitorPhy::%s location event: ",
                                         id_,
                                         __func__);
      }
      break;

    case Events::PathlossEvent::IDENTIFIER:
      {
        Events::PathlossEvent pathlossEvent{serialization};
        pPropagationModelAlgorithm_->update(pathlossEvent.getPathlosses());
        eventTablePublisher_.update(pathlossEvent.getPathlosses());

        LOGGER_STANDARD_LOGGING_FN_VARGS(pPlatformService_->logService(),
                                         DEBUG_LEVEL,
                                         Events::PathlossEventFormatter(pathlossEvent),
                                         "PHYI %03hu MonitorPhy::%s pathloss event: ",
                                         id_,
                                         __func__);
      }
      break;
    }
}

std::uint64_t EMANE::SpectrumTools::MonitorPhy::getQueryIndex(const TimePoint & timePoint)
{
  std::uint64_t count = std::chrono::duration_cast<EMANE::Microseconds>(timePoint.time_since_epoch()).count();

  return std::uint64_t{count / spectrumQueryRate_.count()};
}


EMANE::TimePoint EMANE::SpectrumTools::MonitorPhy::getQueryTime(std::uint64_t u64QueryIndex)
{
  return TimePoint{u64QueryIndex * spectrumQueryRate_};
}


void EMANE::SpectrumTools::MonitorPhy::querySpectrumService()
{
  auto now = Clock::now();

  std::uint64_t binSummaryCount = spectrumQueryRate_.count() / spectrumQueryBinSize_.count();

  std::uint64_t currentQueryIndex{getQueryIndex(now)};

  bool bScheduleQuery{};

  if(!lastQueryIndex_)
    {
      lastQueryIndex_ = currentQueryIndex;
      bScheduleQuery = true;
    }

  // perfom a query
  if(currentQueryIndex - lastQueryIndex_ >= 1)
    {
      EMANESpectrumMonitor::SpectrumEnergy msg{};

      auto startTime =  getQueryTime(lastQueryIndex_);

      msg.set_start_time(std::chrono::duration_cast<Microseconds>(startTime.time_since_epoch()).count());

      msg.set_duration(spectrumQueryBinSize_.count());

      msg.set_sequence(u64SequenceNumber_++);

      for(const auto & iter : spectrumMap_)
        {
          auto pEntry = msg.add_entries();

          pEntry->set_subid(iter.first);

          pEntry->set_bandwidth_hz(std::get<0>(iter.second));

          auto pSpectorMonintor = std::get<1>(iter.second).get();

          for(const auto & frequencyHz : pSpectorMonintor->getFrequencies())
            {
              auto pEnergy = pEntry->add_energies();

              pEnergy->set_frequency_hz(frequencyHz);

              auto window = pSpectorMonintor->request(frequencyHz,
                                                      spectrumQueryRate_ * (currentQueryIndex - lastQueryIndex_),
                                                      startTime);

              for(std::uint64_t i = 0; i < binSummaryCount; ++i)
                {
                  pEnergy->add_energy_mw(maxNoiseBin(window,
                                                     startTime + spectrumQueryBinSize_ * i,
                                                     startTime + spectrumQueryBinSize_ * (i + 1) - Microseconds{1}));
                }
            }
        }

      std::string sSerialization{};

      if(msg.SerializeToString(&sSerialization))
        {
          std::string sTopic{"EMANE.SpectrumTools.MonitorPhy.SpectrumEnergy"};

          if(zmq_send(pZMQSocket_,sTopic.c_str(),sTopic.length(),ZMQ_SNDMORE) < 0 ||
             zmq_send(pZMQSocket_,sSerialization.c_str(),sSerialization.length(),0) < 0)
            {
              LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                      ERROR_LEVEL,
                                      "PHYI %03hu SpectrumTools::MonitorPhy::%s zmq send error %s",
                                      id_,
                                      __func__,
                                      zmq_strerror(errno));
            }

          if(recorderFileStream_.is_open())
            {
              std::uint32_t u32MessageFrameLength = htonl(sSerialization.length());

              recorderFileStream_.write(reinterpret_cast<char *>(&u32MessageFrameLength),
                                        sizeof(u32MessageFrameLength));

              recorderFileStream_.write(sSerialization.c_str(),sSerialization.length());
            }
        }

      // schedule next query
      lastQueryIndex_ = currentQueryIndex;

      bScheduleQuery = true;
    }

  if(bScheduleQuery)
    {
      timedEventId_ =
        pPlatformService_->timerService().
        schedule(std::bind(&MonitorPhy::querySpectrumService,
                           this),
                 getQueryTime(currentQueryIndex + 1));
    }
}


DECLARE_PHY_LAYER(EMANE::SpectrumTools::MonitorPhy);
