/*
 * Copyright (c) 2013-2014,2016-2017,2019-2020 - Adjacent Link LLC,
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

#ifndef EMANE_SPECTRUMTOOLS_MONITORPHY_HEADER_
#define EMANE_SPECTRUMTOOLS_MONITORPHY_HEADER_

#include "emane/phylayerimpl.h"
#include "emane/phytypes.h"
#include "emane/utils/commonlayerstatistics.h"

#include "locationmanager.h"
#include "spectrummonitor.h"
#include "gainmanager.h"
#include "propagationmodelalgorithm.h"
#include "eventtablepublisher.h"
#include "receivepowertablepublisher.h"
#include "fadingmanager.h"

#include <set>
#include <cstdint>
#include <memory>
#include <fstream>

namespace EMANE
{
  namespace SpectrumTools
  {
    class MonitorPhy : public PHYLayerImplementor
    {
    public:
      MonitorPhy(NEMId id,
                 PlatformServiceProvider* pPlatformService);

      ~MonitorPhy();

      void initialize(Registrar & registrar) override;

      void configure(const ConfigurationUpdate & update) override;

      void start() override;

      void stop() override;

      void destroy() throw() override;

      void processConfiguration(const ConfigurationUpdate & update) override;

      void processUpstreamPacket(const CommonPHYHeader & hdr,
                                 UpstreamPacket & pkt,
                                 const ControlMessages & msgs) override;

      void processUpstreamPacket_i(const TimePoint & now,
                                   const CommonPHYHeader & hdr,
                                   UpstreamPacket & pkt,
                                   const ControlMessages & msgs);

      void processDownstreamControl(const ControlMessages & msgs) override;

      void processDownstreamPacket(DownstreamPacket & pkt,
                                   const ControlMessages & msgs) override;

      void processEvent(const EventId & eventId,
                        const Serialization & serialization) override;


      SpectrumMonitor & getSpectrumMonitor();

    private:
      GainManager gainManager_;
      LocationManager locationManager_;
      std::uint64_t u64BandwidthHz_;
      double dReceiverSensitivitydBm_;
      SpectrumMonitor::NoiseMode noiseMode_;
      std::pair<double,bool> optionalFixedAntennaGaindBi_;
      std::unique_ptr<PropagationModelAlgorithm> pPropagationModelAlgorithm_;
      Utils::CommonLayerStatistics commonLayerStatistics_;
      EventTablePublisher eventTablePublisher_;
      ReceivePowerTablePublisher receivePowerTablePublisher_;
      Microseconds noiseBinSize_;
      Microseconds maxSegmentOffset_;
      Microseconds maxMessagePropagation_;
      Microseconds maxSegmentDuration_;
      Microseconds timeSyncThreshold_;
      bool bNoiseMaxClamp_;
      double dSystemNoiseFiguredB_;
      StatisticNumeric<std::uint64_t> * pTimeSyncThresholdRewrite_;
      FadingManager fadingManager_;
      using SpectrumMap = std::map<std::uint16_t, // sub id
                                   std::tuple<std::uint64_t, // bandwidth
                                              FrequencySet, // tx frequencies
                                              std::unique_ptr<SpectrumMonitor>>>;

      SpectrumMap spectrumMap_;
      Microseconds spectrumQueryRate_;
      Microseconds spectrumQueryBinSize_;
      INETAddr spectrumQueryPublishAddr_;
      TimerEventId timedEventId_;
      std::uint64_t lastQueryIndex_;
      void * pZMQContext_;
      void * pZMQSocket_;
      std::uint64_t u64SequenceNumber_;
      std::string sSpectrumQueryRecorderFile_;
      std::fstream recorderFileStream_;

      void querySpectrumService();

      TimePoint getQueryTime(std::uint64_t u64QueryIndex);

      std::uint64_t getQueryIndex(const TimePoint & timePoint);
    };
  }
}
#endif // EMANESPECTRUMMONITORPHY_HEADER_
