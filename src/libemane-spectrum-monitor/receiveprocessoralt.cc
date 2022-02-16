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

#include "receiveprocessoralt.h"
#include "emane/spectrumserviceexception.h"

EMANE::SpectrumTools::ReceiveProcessorAlt::ReceiveProcessorAlt(NEMId id,
                                                               std::uint16_t u16SubId,
                                                               AntennaIndex rxAntennaIndex,
                                                               AntennaManager & antennaManager,
                                                               SpectrumMonitorAlt * pSpectrumMonitorAlt,
                                                               PropagationModelAlgorithm * pPropagationModelAlgorithm,
                                                               FadingAlgorithmStore && fadingAlgorithmStore,
                                                               bool bPopulateReceivePowerMap):
  id_{id},
  u16SubId_{u16SubId},
  rxAntennaIndex_{rxAntennaIndex},
  gainManager_{id,rxAntennaIndex,antennaManager},
  pSpectrumMonitorAlt_{pSpectrumMonitorAlt},
  pPropagationModelAlgorithm_{pPropagationModelAlgorithm},
  fadingAlgorithmStore_{std::move(fadingAlgorithmStore)},
  bPopulateReceivePowerMap_{bPopulateReceivePowerMap},
  u64SpectrumMonitorUpdateSequence_{}{}

EMANE::SpectrumTools::ReceiveProcessorAlt::ProcessResult
EMANE::SpectrumTools::ReceiveProcessorAlt::process(const TimePoint & now,
                                                   const CommonPHYHeader & commonPHYHeader,
                                                   const std::vector<std::pair<LocationInfo,bool>> & locationInfos,
                                                   const std::vector<std::pair<FadingInfo,bool>> & fadingInfos)
{
  ProcessResult result{};

  const auto & frequencyGroups =
    commonPHYHeader.getFrequencyGroups();

  ++u64SpectrumMonitorUpdateSequence_;

  for(const auto & transmitAntenna : commonPHYHeader.getTransmitAntennas())
    {
      auto groupIndex = transmitAntenna.getFrequencyGroupIndex();

      if(groupIndex > frequencyGroups.size())
        {
          result.status_ = ProcessResult::Status::DROP_CODE_ANTENNA_FREQ_INDEX;

          //drop
          return result;
        }

      const auto & frequencySegments = frequencyGroups[groupIndex];

      std::vector<double> rxPowerSegmentsMilliWatt(frequencySegments.size(),0);

      Microseconds propagation{};

      bool bHavePropagationDelay{};

      std::vector<NEMId> transmitters{};

      int iTransmitterIndex{};

      for(const auto & transmitter : commonPHYHeader.getTransmitters())
        {
          transmitters.push_back(transmitter.getNEMId());
          // get the location info for a pair of nodes
          const auto & locationInfo = locationInfos[iTransmitterIndex];
          const auto & fadingInfo = fadingInfos[iTransmitterIndex];
          ++iTransmitterIndex;

          //locationManager_.getLocationInfo(transmitter.getNEMId());

          // get the propagation model pathloss between a pair of nodes for *each* segment
          auto pathlossInfo = (*pPropagationModelAlgorithm_)(transmitter.getNEMId(),
                                                             locationInfo.first,
                                                             frequencySegments);

          // if pathloss is available
          if(pathlossInfo.second)
            {
              // calculate the combined gain (Tx + Rx antenna gain) dBi
              // note: gain manager accesses antenna profiles, knows self node profile info
              //       if available, and is updated with all nodes profile info
              auto gainInfodBi = gainManager_.determineGain(transmitter.getNEMId(),
                                                            transmitAntenna.getIndex(),
                                                            locationInfo.first);

              // if gain is available
              if(std::get<2>(gainInfodBi) == EMANE::GainManager::GainStatus::SUCCESS)
                {
                  result.bGainCacheHit_ = std::get<3>(gainInfodBi);

                  //using ReceivePowerPubisherUpdate = std::tuple<NEMId,std::uint64_t,double>;

                  // set to prevent multiple ReceivePowerTablePublisher updates
                  // for the same NEM frequency pair in a frequency segment list
                  //std::set<ReceivePowerPubisherUpdate> receivePowerTableUpdate{};

                  // frequency segment iterator to map pathloss per segment to
                  // the associated segment
                  FrequencySegments::const_iterator freqIter{frequencySegments.begin()};

                  std::size_t i{};

                  // sum up the rx power for each segment
                  for(const auto & dPathlossdB : pathlossInfo.first)
                    {
                      double dRxPowerSegmentsMilliWatt{};

                      auto optionalSegmentPowerdBm = freqIter->getPowerdBm();

                      double dTxPowerdBm{optionalSegmentPowerdBm.second ?
                        optionalSegmentPowerdBm.first :
                        transmitter.getPowerdBm()};

                      double dPowerdBm{dTxPowerdBm +
                        std::get<0>(gainInfodBi)  +
                        std::get<1>(gainInfodBi) -
                        dPathlossdB};

                      if(fadingInfo.second)
                        {
                          if(fadingInfo.first.first == Events::FadingModel::NONE)
                            {
                              dRxPowerSegmentsMilliWatt = Utils::DB_TO_MILLIWATT(dPowerdBm);
                            }
                          else
                            {
                              if(locationInfo.second)
                                {
                                  const auto iter =
                                    fadingAlgorithmStore_.find(fadingInfo.first.first);

                                  if(iter != fadingAlgorithmStore_.end())
                                    {
                                      //fading algorithms return mW
                                      dRxPowerSegmentsMilliWatt +=
                                        (*iter->second)(dPowerdBm,
                                                        locationInfo.first.getDistanceMeters(),
                                                        fadingInfo.first.second);
                                    }
                                  else
                                    {
                                      result.status_ = ProcessResult::Status::DROP_CODE_FADINGMANAGER_ALGORITHM;

                                      //drop
                                      return result;

                                    }
                                }
                              else
                                {
                                  result.status_ = ProcessResult::Status::DROP_CODE_FADINGMANAGER_LOCATION;

                                  //drop
                                  return result;
                                }
                            }
                        }
                      else
                        {
                          result.status_ = ProcessResult::Status::DROP_CODE_FADINGMANAGER_SELECTION;

                          //drop
                          return result;
                        }

                      rxPowerSegmentsMilliWatt[i++] += dRxPowerSegmentsMilliWatt;

                      if(bPopulateReceivePowerMap_)
                        {
                          result.receivePowerMap_[std::make_tuple(transmitter.getNEMId(),
                                                                  rxAntennaIndex_,
                                                                  transmitAntenna.getIndex(),
                                                                  freqIter->getFrequencyHz())] =std::make_tuple(Utils::MILLIWATT_TO_DB(dRxPowerSegmentsMilliWatt),
                                                                                                                std::get<0>(gainInfodBi),
                                                                                                                std::get<1>(gainInfodBi),
                                                                                                                dTxPowerdBm,
                                                                                                                dPathlossdB);
                        }

                      ++freqIter;
                    }

                  // calculate propagation delay from 1 of the transmitters
                  //  note: these are collaborative (constructive) transmissions, all
                  //        the messages are arriving at or near the same time. Destructive
                  //        transmission should be sent as multiple messages
                  if(locationInfo.second && !bHavePropagationDelay)
                    {
                      if(locationInfo.first.getDistanceMeters() > 0.0)
                        {
                          propagation =
                            Microseconds{static_cast<std::uint64_t>(std::round(locationInfo.first.getDistanceMeters() / SOL_MPS * 1000000))};
                        }

                      bHavePropagationDelay = true;
                    }
                }
              else
                {
                  // drop due to GainManager not enough info
                  switch(std::get<2>(gainInfodBi))
                    {
                    case GainManager::GainStatus::ERROR_LOCATIONINFO:
                      result.status_ = ProcessResult::Status::DROP_CODE_GAINMANAGER_LOCATION;
                      break;
                    case GainManager::GainStatus::ERROR_PROFILEINFO:
                      result.status_ = ProcessResult::Status::DROP_CODE_GAINMANAGER_ANTENNAPROFILE;
                      break;
                    case GainManager::GainStatus::ERROR_HORIZON:
                      result.status_ = ProcessResult::Status::DROP_CODE_GAINMANAGER_HORIZON;
                      break;
                    case GainManager::GainStatus::ERROR_ANTENNA_INDEX:
                      result.status_ = ProcessResult::Status::DROP_CODE_GAINMANAGER_ANTENNA_INDEX;
                      break;
                    default:
                      break;
                    }

                  // drop
                  return result;
                }
            }
          else
            {
              // drop due to PropagationModelAlgorithm not enough info
              result.status_ = ProcessResult::Status::DROP_CODE_PROPAGATIONMODEL;

              // drop
              return result;
            }
        }


      // update the spectrum monitor with the signal information
      // note: spectrum monitor will remove any frequency segments that are
      //       below the receiver sensitivity. Any frequency segment not in
      //       the foi will cause the entire message to be treated as out-of-band
      //       regardless of the subid. Spectrum monitor will adjust SoT, propagation
      //       delay, offset and duration if out of acceptable value range.
      try
        {
          auto spectrumInfo = pSpectrumMonitorAlt_->update(now,
                                                           commonPHYHeader.getTxTime(),
                                                           propagation,
                                                           frequencySegments,
                                                           transmitAntenna.getBandwidthHz(),
                                                           rxPowerSegmentsMilliWatt,
                                                           transmitters,
                                                           transmitAntenna.getIndex(),
                                                           transmitAntenna.getSpectralMaskIndex());

        }
      catch(SpectrumServiceException & exp)
        {
          result.status_ = ProcessResult::Status::DROP_CODE_SPECTRUM_CLAMP;

          // drop
          return result;
        }
    }

  // below receiver sensitivity is still considered a success at this
  // point since multiple antennas may be in use
  result.status_ = ProcessResult::Status::SUCCESS;

  return result;
}
