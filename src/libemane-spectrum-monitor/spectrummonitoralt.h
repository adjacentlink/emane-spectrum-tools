/*
 * Copyright (c) 2013-2014,2019-2020 - Adjacent Link LLC, Bridgewater,
 * New Jersey
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

#ifndef EMANESPECTRUMTOOLSSPECTRUMMONITOR_HEADER_
#define EMANESPECTRUMTOOLSSPECTRUMMONITOR_HEADER_

#include "emane/types.h"
#include "emane/frequencysegment.h"
#include "emane/spectrumserviceprovider.h"
#include "noiserecorder.h"

#include <set>
#include <map>
#include <vector>
#include <memory>
#include <tuple>
#include <mutex>

namespace EMANE
{
  namespace SpectrumTools
  {
    using SpectrumUpdate =
      std::tuple<TimePoint,Microseconds,Microseconds,FrequencySegments,bool,double>;

    class SpectrumMonitorAlt
    {
    public:
      SpectrumMonitorAlt(uint16_t u16SubId,
                         const Microseconds & binSize,
                         const Microseconds & maxOffset,
                         const Microseconds & maxPropagation,
                         const Microseconds & maxDuration,
                         const Microseconds & timeSyncThreshold,
                         bool bMaxClamp);

      std::tuple<TimePoint,Microseconds,Microseconds,FrequencySegments,bool>
      update(const TimePoint & now,
             const TimePoint & txTime,
             const Microseconds & propagationDelay,
             const FrequencySegments & segments,
             std::uint64_t u64SegmentBandwidthHz,
             const std::vector<double> & rxPowersMilliWatt,
             const std::vector<NEMId> & transmitters);


      SpectrumUpdate
      update(const TimePoint & now,
             const TimePoint & txTime,
             const Microseconds & propagationDelay,
             const FrequencySegments & segments,
             std::uint64_t u64SegmentBandwidthHz,
             const std::vector<double> & rxPowersMilliWatt,
             const std::vector<NEMId> & transmitters,
             AntennaIndex txAntennaIndex);


      FrequencySet getFrequencies() const;// override;

      // test harness access
      SpectrumWindow request_i(const TimePoint & now,
                               std::uint64_t u64FrequencyHz,
                               const Microseconds & duration = Microseconds::zero(),
                               const TimePoint & timepoint = TimePoint::min()) const;

      SpectrumWindow request(std::uint64_t u64FrequencyHz,
                             const Microseconds & duration = Microseconds::zero(),
                             const TimePoint & timepoint = TimePoint::min()) const;// override;

      std::vector<double> dump(std::uint64_t u64FrequencyHz) const;

    private:
      using NoiseRecorderMap = std::map<std::uint64_t,std::unique_ptr<NoiseRecorder>>;


      Microseconds binSize_;
      Microseconds maxOffset_;
      Microseconds maxPropagation_;
      Microseconds maxDuration_;
      bool bMaxClamp_;
      Microseconds timeSyncThreshold_;
      NoiseRecorderMap noiseRecorderMap_;
      uint16_t u16SubId_;
    };
  }
}
#endif // EMANESPECTRUMTOOLSSPECTRUMMONITOR_HEADER_
