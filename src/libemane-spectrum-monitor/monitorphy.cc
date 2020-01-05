/*
 * Copyright (c) 2013-2014,2016-2017,2019 - Adjacent Link LLC,
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
#include "emane/startexception.h"
#include "emane/spectrumserviceexception.h"

#include "emane/events/antennaprofileevent.h"
#include "emane/events/antennaprofileeventformatter.h"
#include "emane/events/locationevent.h"
#include "emane/events/locationeventformatter.h"
#include "emane/events/pathlossevent.h"
#include "emane/events/pathlosseventformatter.h"
#include "emane/events/fadingselectionevent.h"
#include "emane/events/fadingselectioneventformatter.h"

#include "freespacepropagationmodelalgorithm.h"
#include "tworaypropagationmodelalgorithm.h"
#include "precomputedpropagationmodelalgorithm.h"
#include "emane/utils/spectrumwindowutils.h"

#include "spectrummonitor.pb.h"
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
                                                     "Fade Select"};

  const std::string FADINGMANAGER_PREFIX{"fading."};
}

EMANE::SpectrumTools::MonitorPhy::MonitorPhy(NEMId id,
                                             PlatformServiceProvider * pPlatformService):
  PHYLayerImplementor{id, pPlatformService},
  gainManager_{id},
  locationManager_{id},
  u64BandwidthHz_{},
  dReceiverSensitivitydBm_{},
  commonLayerStatistics_{STATISTIC_TABLE_LABELS,{},"0"},
  eventTablePublisher_{id},
  noiseBinSize_{},
  maxSegmentOffset_{},
  maxMessagePropagation_{},
  maxSegmentDuration_{},
  timeSyncThreshold_{},
  bNoiseMaxClamp_{},
  dSystemNoiseFiguredB_{},
  fadingManager_{id, pPlatformService,FADINGMANAGER_PREFIX},
  pZMQContext_{},
  pZMQSocket_{},
  u64SequenceNumber_{}{}

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

  configRegistrar.registerNumeric<std::uint64_t>("bandwidth",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {1000000},
                                                 "Defines receiver bandwidth in Hz and also serves as the"
                                                 " default bandwidth for OTA transmissions when not provided"
                                                 " by the MAC.",
                                                 1);

  configRegistrar.registerNumeric<std::uint64_t>("noisebinsize",
                                                 EMANE::ConfigurationProperties::DEFAULT,
                                                 {20},
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
                                                 " time instead of the time contained in the Common PHY Header."
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

  configRegistrar.registerNumeric<double>("systemnoisefigure",
                                          EMANE::ConfigurationProperties::DEFAULT,
                                          {4.0},
                                          "Defines the system noise figure in dB and is used to determine the"
                                          " receiver sensitivity.");

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

  fadingManager_.initialize(registrar);
}

void EMANE::SpectrumTools::MonitorPhy::configure(const ConfigurationUpdate & update)
{
  ConfigurationUpdate fadingManagerConfiguration{};

  for(const auto & item : update)
    {
      if(item.first == "bandwidth")
        {
          u64BandwidthHz_ = item.second[0].asUINT64();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %ju Hz",
                                  id_,
                                  __func__,
                                  item.first.c_str(),
                                  u64BandwidthHz_);
        }
      else if(item.first == "fixedantennagain")
        {
          optionalFixedAntennaGaindBi_.first = item.second[0].asDouble();

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  INFO_LEVEL,
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %3.2f dBi",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %s",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %s",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %ju usec",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %s",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %ju usec",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %ju usec",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %ju usec",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %ju usec",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %3.2f dB",
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
      else
        {
          if(!item.first.compare(0,FADINGMANAGER_PREFIX.size(),FADINGMANAGER_PREFIX))
            {
              fadingManagerConfiguration.push_back(item);
            }
          else
            {
              throw makeException<ConfigureException>("SpectrumTools::MonitorPhy: Unexpected configuration item %s",
                                                      item.first.c_str());
            }
        }
    }

  fadingManager_.configure(fadingManagerConfiguration);

  dReceiverSensitivitydBm_ = THERMAL_NOISE_DB + dSystemNoiseFiguredB_ + 10.0 * log10(u64BandwidthHz_);

  if((maxSegmentOffset_ + maxMessagePropagation_ + 2 * maxSegmentDuration_) % noiseBinSize_ !=
     Microseconds::zero())
    {
      throw makeException<ConfigureException>("noisemaxsegmentoffset + noisemaxmessagepropagation"
                                              " + 2 * noisemaxsegmentduration not evenly divisible by the"
                                              " noisebinsize");
    }

}

void EMANE::SpectrumTools::MonitorPhy::start()
{
  LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                          DEBUG_LEVEL,
                          "PHYI %03hu SpectrumTools::MonitorPhy::%s",
                          id_,
                          __func__);

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


  querySpectrumService();
}

void EMANE::SpectrumTools::MonitorPhy::stop()
{
  LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                          DEBUG_LEVEL,
                          "PHYI %03hu SpectrumTools::MonitorPhy::%s",
                          id_,
                          __func__);

  if(pZMQSocket_)
    {
      zmq_close(pZMQSocket_);
      pZMQSocket_ = nullptr;
    }

  if(pZMQContext_)
    {
      zmq_ctx_destroy(pZMQContext_);
      pZMQContext_ = nullptr;
    }
}

void EMANE::SpectrumTools::MonitorPhy::destroy() throw()
{
  LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                          DEBUG_LEVEL,
                          "PHYI %03hu SpectrumTools::MonitorPhy::%s",
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
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s: %s = %3.2f dBi",
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

void EMANE::SpectrumTools::MonitorPhy::processDownstreamControl(const ControlMessages & )
{}

void EMANE::SpectrumTools::MonitorPhy::processDownstreamPacket(DownstreamPacket &,
                                                               const ControlMessages &)
{}


void EMANE::SpectrumTools::MonitorPhy::processUpstreamPacket(const CommonPHYHeader & commonPHYHeader,
                                                             UpstreamPacket & pkt,
                                                             const ControlMessages &)
{
  processUpstreamPacket_i(Clock::now(),commonPHYHeader,pkt,{});
}

void EMANE::SpectrumTools::MonitorPhy::processUpstreamPacket_i(const TimePoint & now,
                                                               const CommonPHYHeader & commonPHYHeader,
                                                               UpstreamPacket & pkt,
                                                               const ControlMessages &)
{
  LOGGER_VERBOSE_LOGGING_FN_VARGS(pPlatformService_->logService(),
                                  DEBUG_LEVEL,
                                  std::bind(&CommonPHYHeader::format, std::ref(commonPHYHeader)),
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s Common PHY Header",
                                  id_,
                                  __func__);

  commonLayerStatistics_.processInbound(pkt);

  const  auto & pktInfo = pkt.getPacketInfo();

  bool bDrop{};

  const auto & frequencySegments = commonPHYHeader.getFrequencySegments();

  std::vector<double> rxPowerSegments(frequencySegments.size(),0);

  Microseconds propagation{};

  bool bHavePropagationDelay{};

  std::vector<NEMId> transmitters{};

  for(const auto & transmitter :  commonPHYHeader.getTransmitters())
    {
      transmitters.push_back(transmitter.getNEMId());

      // get the location info for a pair of nodes
      auto locationPairInfoRet = locationManager_.getLocationInfo(transmitter.getNEMId());

      // get the propagation model pathloss between a pair of nodes for *each* segment
      auto pathlossInfo = (*pPropagationModelAlgorithm_)(transmitter.getNEMId(),
                                                         locationPairInfoRet.first,
                                                         frequencySegments);

      // if pathloss is available
      if(pathlossInfo.second)
        {
          // calculate the combined gain (Tx + Rx antenna gain) dBi
          // note: gain manager accesses antenna profiles, knows self node profile info
          //       if available, and is updated with all nodes profile info
          auto gainInfodBi = gainManager_.determineGain(transmitter.getNEMId(),
                                                        locationPairInfoRet.first,
                                                        optionalFixedAntennaGaindBi_,
                                                        commonPHYHeader.getOptionalFixedAntennaGaindBi());

          // if gain is available
          if(gainInfodBi.second == EMANE::GainManager::GainStatus::SUCCESS)
            {
              using ReceivePowerPubisherUpdate = std::tuple<NEMId,std::uint64_t,double>;

              // set to prevent multiple ReceivePowerTablePublisher updates
              // for the same NEM frequency pair in a frequency segment list
              std::set<ReceivePowerPubisherUpdate> receivePowerTableUpdate{};

              // frequency segment iterator to map pathloss per segment to
              // the associated segment
              FrequencySegments::const_iterator freqIter{frequencySegments.begin()};

              std::size_t i{};

              // sum up the rx power for each segment
              for(const auto & dPathlossdB : pathlossInfo.first)
                {
                  auto optionalSegmentPowerdBm = freqIter->getPowerdBm();

                  double powerdBm{(optionalSegmentPowerdBm.second ?
                                   optionalSegmentPowerdBm.first :
                                   transmitter.getPowerdBm()) +
                                  gainInfodBi.first -
                                  dPathlossdB};

                  auto powerdBmInfo = fadingManager_.calculate(transmitter.getNEMId(),
                                                               powerdBm,
                                                               locationPairInfoRet);

                  if(powerdBmInfo.second == FadingManager::FadingStatus::SUCCESS)
                    {
                      receivePowerTableUpdate.insert(ReceivePowerPubisherUpdate{transmitter.getNEMId(),
                                                                                freqIter->getFrequencyHz(),
                                                                                powerdBmInfo.first});

                      ++freqIter;

                      rxPowerSegments[i++] += Utils::DB_TO_MILLIWATT(powerdBmInfo.first);
                    }
                  else
                    {
                      // drop due to FadingManager not enough info
                      bDrop = true;

                      std::uint16_t u16Code{};

                      const char * pzReason{};

                      switch(powerdBmInfo.second)
                        {
                        case FadingManager::FadingStatus::ERROR_LOCATIONINFO:
                          pzReason = "fading missing location info";
                          u16Code = DROP_CODE_FADINGMANAGER_LOCATION;
                          break;
                        case FadingManager::FadingStatus::ERROR_ALGORITHM:
                          pzReason = "fading algorithm error/missing info";
                          u16Code = DROP_CODE_FADINGMANAGER_ALGORITHM;
                          break;
                        case FadingManager::FadingStatus::ERROR_SELECTION:
                          pzReason = "fading algorithm selection unkown";
                          u16Code = DROP_CODE_FADINGMANAGER_SELECTION;
                          break;
                        default:
                          pzReason = "unknown";
                          break;
                        }

                      commonLayerStatistics_.processOutbound(pkt,
                                                             std::chrono::duration_cast<Microseconds>(Clock::now() - now),
                                                             u16Code);

                      LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                              DEBUG_LEVEL,
                                              "PHYI %03hu SpectrumTools::MonitorPhy::%s transmitter %hu,"
                                              " src %hu, dst %hu, drop %s",
                                              id_,
                                              __func__,
                                              transmitter.getNEMId(),
                                              pktInfo.getSource(),
                                              pktInfo.getDestination(),
                                              pzReason);

                      break;
                    }
                }

              for(const auto & entry : receivePowerTableUpdate)
                {
                  receivePowerTablePublisher_.update(std::get<0>(entry),
                                                     std::get<1>(entry),
                                                     std::get<2>(entry),
                                                     commonPHYHeader.getTxTime());
                }


              // calculate propagation delay from 1 of the transmitters
              //  note: these are collaborative (constructive) transmissions, all
              //        the messages are arriving at or near the same time. Destructive
              //        transmission should be sent as multiple messages
              if(locationPairInfoRet.second && !bHavePropagationDelay)
                {
                  if(locationPairInfoRet.first.getDistanceMeters() > 0.0)
                    {
                      propagation =
                        Microseconds{static_cast<std::uint64_t>(std::round(locationPairInfoRet.first.getDistanceMeters() / SOL_MPS * 1000000))};
                    }

                  bHavePropagationDelay = true;
                }
            }
          else
            {
              // drop due to GainManager not enough info
              bDrop = true;

              std::uint16_t u16Code{};

              const char * pzReason{};

              switch(gainInfodBi.second)
                {
                case GainManager::GainStatus::ERROR_LOCATIONINFO:
                  pzReason = "missing location info";
                  u16Code = DROP_CODE_GAINMANAGER_LOCATION;
                  break;
                case GainManager::GainStatus::ERROR_PROFILEINFO:
                  pzReason = "missing profile info";
                  u16Code = DROP_CODE_GAINMANAGER_ANTENNAPROFILE;
                  break;
                case GainManager::GainStatus::ERROR_HORIZON:
                  pzReason = "below the horizon";
                  u16Code = DROP_CODE_GAINMANAGER_HORIZON;
                  break;
                default:
                  pzReason = "unknown";
                  break;
                }

              commonLayerStatistics_.processOutbound(pkt,
                                                     std::chrono::duration_cast<Microseconds>(Clock::now() - now),
                                                     u16Code);

              LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                      DEBUG_LEVEL,
                                      "PHYI %03hu SpectrumTools::MonitorPhy::%s transmitter %hu, src %hu, dst %hu, drop %s",
                                      id_,
                                      __func__,
                                      transmitter.getNEMId(),
                                      pktInfo.getSource(),
                                      pktInfo.getDestination(),
                                      pzReason);


              break;
            }
        }
      else
        {
          // drop due to PropagationModelAlgorithm not enough info
          bDrop = true;

          commonLayerStatistics_.processOutbound(pkt,
                                                 std::chrono::duration_cast<Microseconds>(Clock::now() - now),
                                                 DROP_CODE_PROPAGATIONMODEL);

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  DEBUG_LEVEL,
                                  "PHYI %03hu SpectrumTools::MonitorPhy::%s transmitter %hu, src %hu, dst %hu,"
                                  " drop propagation model missing info",
                                  id_,
                                  __func__,
                                  transmitter.getNEMId(),
                                  pktInfo.getSource(),
                                  pktInfo.getDestination());
          break;
        }
    }

  if(!bDrop)
    {
      try
        {
          FrequencySet txFrequencies{};

          auto iter  = spectrumMap_.find(commonPHYHeader.getSubId());

          if(iter == spectrumMap_.end())
            {
              iter = spectrumMap_.insert(std::make_pair(commonPHYHeader.getSubId(),
                                                        make_tuple(commonPHYHeader.getBandwidthHz(),
                                                                   FrequencySet{},
                                                                   std::unique_ptr<SpectrumMonitor>(new SpectrumMonitor())))).first;
            }
          else if(std::get<0>(iter->second) != commonPHYHeader.getBandwidthHz())
            {
              std::get<0>(iter->second) = commonPHYHeader.getBandwidthHz();
            }

          int iNewTxFrequencies{};

          for(const auto & segment : frequencySegments)
            {
              txFrequencies.insert(segment.getFrequencyHz());

              iNewTxFrequencies += std::get<1>(iter->second).count(segment.getFrequencyHz()) == 0;
            }


          if(!iNewTxFrequencies)
            {
              // not really out of band, it is all in band with no
              // out of band using outofband disabled logic that
              // removes in band signal energy
              std::get<2>(iter->second)->update(now,
                                                commonPHYHeader.getTxTime(),
                                                propagation,
                                                frequencySegments,
                                                commonPHYHeader.getBandwidthHz(),
                                                rxPowerSegments,
                                                false,
                                                transmitters,
                                                commonPHYHeader.getSubId());
            }
          else
            {
              // add new frequencies
              std::get<1>(iter->second).insert(txFrequencies.begin(),
                                               txFrequencies.end());

              //re-intialize with new freq set
              std::get<2>(iter->second)->initialize(0,
                                                    std::get<1>(iter->second),
                                                    u64BandwidthHz_,
                                                    Utils::DB_TO_MILLIWATT(dReceiverSensitivitydBm_),
                                                    SpectrumMonitor::NoiseMode::OUTOFBAND,
                                                    noiseBinSize_,
                                                    maxSegmentOffset_,
                                                    maxMessagePropagation_,
                                                    maxSegmentDuration_,
                                                    timeSyncThreshold_,
                                                    bNoiseMaxClamp_);
            }
        }
      catch(SpectrumServiceException & exp)
        {
          commonLayerStatistics_.processOutbound(pkt,
                                                 std::chrono::duration_cast<Microseconds>(Clock::now() - now),
                                                 DROP_CODE_SPECTRUM_CLAMP);

          LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                                  DEBUG_LEVEL,
                                  "PHYI %03hu FrameworkPHY::%s src %hu, dst %hu,"
                                  " drop spectrum out of bound %s",
                                  id_,
                                  __func__,
                                  pktInfo.getSource(),
                                  pktInfo.getDestination(),
                                  exp.what());
        }
    }
}

void EMANE::SpectrumTools::MonitorPhy::processEvent(const EventId & eventId,
                                                    const Serialization & serialization)
{
  LOGGER_STANDARD_LOGGING(pPlatformService_->logService(),
                          DEBUG_LEVEL,
                          "PHYI %03hu SpectrumTools::MonitorPhy::%s event id: %hu",
                          id_,
                          __func__,
                          eventId);

  switch(eventId)
    {
    case Events::AntennaProfileEvent::IDENTIFIER:
      {
        Events::AntennaProfileEvent antennaProfile{serialization};
        gainManager_.update(antennaProfile.getAntennaProfiles());
        eventTablePublisher_.update(antennaProfile.getAntennaProfiles());

        LOGGER_STANDARD_LOGGING_FN_VARGS(pPlatformService_->logService(),
                                         DEBUG_LEVEL,
                                         Events::AntennaProfileEventFormatter(antennaProfile),
                                         "PHYI %03hu SpectrumTools::MonitorPhy::%s antenna profile event: ",
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
                                         "PHYI %03hu SpectrumTools::MonitorPhy::%s fading selection event: ",
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
                                         "PHYI %03hu SpectrumTools::MonitorPhy::%s location event: ",
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
                                         "PHYI %03hu SpectrumTools::MonitorPhy::%s pathloss event: ",
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

      msg.set_receiver_sensitivity_dbm(dReceiverSensitivitydBm_);

      msg.set_sequence(u64SequenceNumber_++);

      for(const auto & iter : spectrumMap_)
        {
          auto pEntry = msg.add_entries();

          pEntry->set_subid(iter.first);

          pEntry->set_bandwidth_hz(std::get<0>(iter.second));

          for(const auto & frequencyHz : std::get<1>(iter.second))
            {
              auto pEnergy = pEntry->add_energies();

              pEnergy->set_frequency_hz(frequencyHz);

              auto window = std::get<2>(iter.second)->request(frequencyHz,
                                                              spectrumQueryRate_ * (currentQueryIndex - lastQueryIndex_),
                                                              startTime);

              for(std::uint64_t i = 0; i < binSummaryCount; ++i)
                {
                  pEnergy->add_energy_dbm(std::get<0>(Utils::maxBinNoiseFloorRange(window,
                                                                                   0,
                                                                                   startTime + spectrumQueryBinSize_ * i,
                                                                                   startTime + spectrumQueryBinSize_ * (i + 1) - Microseconds{1})));
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
