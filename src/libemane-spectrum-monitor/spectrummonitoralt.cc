/*
 * Copyright (c) 2013-2015,2019-2020 - Adjacent Link LLC, Bridgewater,
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

#include "spectrummonitoralt.h"
#include "noiserecorder.h"

#include "emane/spectrumserviceexception.h"
#include "emane/utils/conversionutils.h"

#include <algorithm>
#include <functional>

EMANE::SpectrumTools::SpectrumMonitorAlt::SpectrumMonitorAlt(std::uint16_t u16SubId,
                                                             const Microseconds & binSize,
                                                             const Microseconds & maxOffset,
                                                             const Microseconds & maxPropagation,
                                                             const Microseconds & maxDuration,
                                                             const Microseconds & timeSyncThreshold,
                                                             bool bMaxClamp):
  binSize_{binSize},
  maxOffset_{maxOffset},
  maxPropagation_{maxPropagation},
  maxDuration_{maxDuration},
  bMaxClamp_{bMaxClamp},
  timeSyncThreshold_{timeSyncThreshold},
  u16SubId_{u16SubId}{}


std::tuple<EMANE::TimePoint,EMANE::Microseconds,EMANE::Microseconds,EMANE::FrequencySegments,bool>
EMANE::SpectrumTools::SpectrumMonitorAlt::update(const TimePoint & now,
                                                 const TimePoint & txTime,
                                                 const Microseconds & propagationDelay,
                                                 const FrequencySegments & segments,
                                                 std::uint64_t u64SegmentBandwidthHz,
                                                 const std::vector<double> & rxPowersMilliWatt,
                                                 const std::vector<NEMId> & transmitters)
{
  if(segments.size() != rxPowersMilliWatt.size())
    {
      return std::make_tuple(TimePoint{},Microseconds{},Microseconds{},FrequencySegments{},false);
    }

  // validate txTime in case of time sync issues
  auto validTxTime = txTime;

  if(txTime > now + timeSyncThreshold_ || now - txTime > timeSyncThreshold_)
    {
      validTxTime = now;
    }

  auto validPropagation = propagationDelay;

  if(propagationDelay > maxPropagation_)
    {
      if(bMaxClamp_)
        {
          validPropagation = maxPropagation_;
        }
      else
        {
          throw makeException<SpectrumServiceException>("Message propagation %ju usec > max propagation %ju usec and max clamp is %s",
                                                        propagationDelay.count(),
                                                        maxPropagation_.count(),
                                                        bMaxClamp_ ? "on" : "off");
        }
    }

  TimePoint maxEoR{};
  TimePoint minSoR{TimePoint::max()};

  size_t i{};

  for(const auto & segment : segments)
    {
      TimePoint startOfReception{};
      TimePoint endOfReception{};

      auto validOffset = segment.getOffset();

      if(validOffset > maxOffset_)
        {
          if(bMaxClamp_)
            {
              validOffset = maxOffset_;
            }
          else
            {
              throw makeException<SpectrumServiceException>("Segment offset %ju usec > max offset %ju usec and max clamp is %s",
                                                            validOffset.count(),
                                                            maxOffset_.count(),
                                                            bMaxClamp_ ? "on" : "off");
            }
        }

      auto validDuration = segment.getDuration();

      if(validDuration > maxDuration_)
        {
          if(bMaxClamp_)
            {
              validDuration = maxDuration_;
            }
          else
            {
              throw makeException<SpectrumServiceException>("Segment duration %ju usec > max duration %ju usec and max clamp is %s",
                                                            validDuration.count(),
                                                            maxDuration_.count(),
                                                            bMaxClamp_ ? "on" : "off");
            }
        }


      if(rxPowersMilliWatt[i])
        {
          auto iter = noiseRecorderMap_.find(segment.getFrequencyHz());

          if(iter == noiseRecorderMap_.end())
            {
              iter = noiseRecorderMap_.insert(std::make_pair(segment.getFrequencyHz(),
                                                             std::unique_ptr<NoiseRecorder>{new NoiseRecorder{binSize_,
                                                                   maxOffset_,
                                                                   maxPropagation_,
                                                                   maxDuration_,
                                                                   0,
                                                                   segment.getFrequencyHz(),
                                                                   u64SegmentBandwidthHz,
                                                                   0}})).first;
            }


          double u64UpperFrequencyHz{segment.getFrequencyHz() + u64SegmentBandwidthHz / 2.0};
          double u64LowerFrequencyHz{segment.getFrequencyHz() - u64SegmentBandwidthHz / 2.0};

          std::tie(startOfReception,endOfReception) =
            iter->second->update(now,
                                 validTxTime,
                                 validOffset,
                                 validPropagation,
                                 validDuration,
                                 rxPowersMilliWatt[i],
                                 transmitters,
                                 u64LowerFrequencyHz,
                                 u64UpperFrequencyHz);
        }

      ++i;
    }


  return std::make_tuple(validTxTime,
                         validPropagation,
                         std::chrono::duration_cast<Microseconds>(maxEoR - minSoR),
                         FrequencySegments{},
                         false);
}

EMANE::FrequencySet
EMANE::SpectrumTools::SpectrumMonitorAlt::getFrequencies() const
{
  FrequencySet frequencySet;

  for(auto & entry : noiseRecorderMap_)
    {
      frequencySet.insert(entry.first);
    }

  return frequencySet;
}

EMANE::SpectrumWindow
EMANE::SpectrumTools::SpectrumMonitorAlt::request_i(const TimePoint & now,
                                                    std::uint64_t u64FrequencyHz,
                                                    const Microseconds & duration,
                                                    const TimePoint & timepoint) const
{
  auto validDuration = duration;

  if(validDuration > maxDuration_)
    {
      if(bMaxClamp_)
        {
          validDuration = maxDuration_;
        }
      else
        {
          throw makeException<SpectrumServiceException>("Segment duration %ju usec > max duration %ju usec and max clamp is %s",
                                                        validDuration.count(),
                                                        maxDuration_.count(),
                                                        bMaxClamp_ ? "on" : "off");
        }
    }

  const auto iter = noiseRecorderMap_.find(u64FrequencyHz);

  if(iter!= noiseRecorderMap_.end())
    {
      auto ret = iter->second->get(now,validDuration,timepoint);

      return std::tuple_cat(std::move(ret),std::make_tuple(binSize_,0,true));
    }
  else
    {
      return SpectrumWindow{{},{},{},{},false};
    }
}

EMANE::SpectrumWindow
EMANE::SpectrumTools::SpectrumMonitorAlt::request(std::uint64_t u64FrequencyHz,
                                                  const Microseconds & duration,
                                                  const TimePoint & timepoint) const
{
  return request_i(Clock::now(),u64FrequencyHz,duration,timepoint);
}

std::vector<double> EMANE::SpectrumTools::SpectrumMonitorAlt::dump(std::uint64_t u64FrequencyHz) const
{
  const auto iter = noiseRecorderMap_.find(u64FrequencyHz);

  if(iter!= noiseRecorderMap_.end())
    {
      return iter->second->dump();
    }

  return {};
}
