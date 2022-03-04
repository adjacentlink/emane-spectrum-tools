/*
 * Copyright (c) 2013-2017,2022 - Adjacent Link LLC, Bridgewater,
 *  New Jersey
 * Copyright (c) 2008-2012 - DRS CenGen, LLC, Columbia, Maryland
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
 * * Neither the name of DRS CenGen, LLC nor the names of its
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

#include "otaextractor.h"
#include "eventextractor.h"
#include "logservice.h"
#include "otaheader.pb.h"

#include <emane/net.h>
#include <emane/controls/serializedcontrolmessage.h>

#include <sstream>
#include <algorithm>
#include <uuid.h>

namespace
{
  struct PartInfo
  {
    std::uint8_t u8More_; /**< More parts to follow*/
    std::uint32_t u32Offset_; /**< Offset of payload */
    std::uint32_t u32Size_;  /**< Part size */
  } __attribute__((packed));

  std::string stringFromVectorIO(size_t size,
                                 size_t & index,
                                 size_t & offset,
                                 const EMANE::Utils::VectorIO & vectorIO)
  {
    std::string buf{};

    size_t targetBytes{size};

    while(targetBytes)
      {
        size_t available{vectorIO[index].iov_len - offset};

        if(available)
          {
            if(available >= targetBytes)
              {
                buf.insert(buf.end(),
                           &reinterpret_cast<uint8_t *>(vectorIO[index].iov_base)[offset],
                           &reinterpret_cast<uint8_t *>(vectorIO[index].iov_base)[offset+targetBytes]);

                offset += targetBytes;

                targetBytes = 0;
              }
            else
              {
                buf.insert(buf.end(),
                           &reinterpret_cast<uint8_t *>(vectorIO[index].iov_base)[offset],
                           &reinterpret_cast<uint8_t *>(vectorIO[index].iov_base)[offset] + available);

                targetBytes -= available;

                ++index;

                offset = 0;
              }
          }
      }

    return buf;
  }
}

EMANE::SpectrumTools::Recorder::OTAExtractor::OTAExtractor(const Seconds & partCheckThreshold,
                                                           const Seconds & partTimeoutThreshold):
  partCheckThreshold_{partCheckThreshold},
  lastPartCheckTime_{partTimeoutThreshold}{}

EMANE::SpectrumTools::Recorder::Records
EMANE::SpectrumTools::Recorder::OTAExtractor::process(const void * buf, size_t len)
{
  auto now =  Clock::now();
  Records records;

  // ota message len sanity check
  if(static_cast<size_t>(len) >= sizeof(std::uint16_t))
    {
      std::uint16_t u16OTAHeaderLength =
        NTOHS(*reinterpret_cast<const std::uint16_t *>(buf));

      len -= sizeof(std::uint16_t);

      EMANEMessage::OTAHeader otaHeader;

      size_t payloadIndex{2 + u16OTAHeaderLength + sizeof(PartInfo)};

      if(static_cast<size_t>(len) >= u16OTAHeaderLength + sizeof(PartInfo) &&
         otaHeader.ParseFromArray(&reinterpret_cast<const char *>(buf)[2], u16OTAHeaderLength))
        {
          PartInfo partInfo = *reinterpret_cast<const PartInfo *>(&reinterpret_cast<const char *>(buf)[2+u16OTAHeaderLength]);

          partInfo.u32Offset_ = NTOHL(partInfo.u32Offset_);
          partInfo.u32Size_ = NTOHL(partInfo.u32Size_);

          // verify we have the advertized part length
          if(static_cast<size_t>(len) ==
             u16OTAHeaderLength +
             sizeof(PartInfo) +
             partInfo.u32Size_)
            {
              // message contained in a single part
              if(!partInfo.u8More_  && !partInfo.u32Offset_)
                {
                  auto & payloadInfo = otaHeader.payloadinfo();
                  handleOTAMessage(now,
                                   payloadInfo.eventlength(),
                                   payloadInfo.controllength(),
                                   payloadInfo.datalength(),
                                   otaHeader.source(),
                                   otaHeader.destination(),
                                   otaHeader.sequence(),
                                   {{const_cast<char *>(&reinterpret_cast<const char *>(buf)[payloadIndex]),
                                        partInfo.u32Size_}},
                                   records);
                }
              else
                {
                  PartKey partKey = PartKey{otaHeader.source(),otaHeader.sequence()};

                  auto iter = partStore_.find(partKey);

                  if(iter != partStore_.end())
                    {
                      size_t & totalReceivedPartsBytes{std::get<0>(iter->second)};
                      size_t & totalEventBytes{std::get<1>(iter->second)};
                      size_t & totalControlBytes{std::get<2>(iter->second)};
                      size_t & totalDataBytes{std::get<3>(iter->second)};
                      auto & parts = std::get<4>(iter->second);
                      auto & lastPartTime = std::get<5>(iter->second);

                      // check to see if first part has been received
                      if(otaHeader.has_payloadinfo())
                        {
                          auto & payloadInfo = otaHeader.payloadinfo();
                          totalEventBytes = payloadInfo.eventlength();
                          totalControlBytes = payloadInfo.controllength();
                          totalDataBytes = payloadInfo.datalength();
                        }

                      // update last part receive time
                      lastPartTime = now;

                      // add this part to parts and update receive count
                      totalReceivedPartsBytes +=  partInfo.u32Size_;

                      parts.insert(std::make_pair(static_cast<size_t>(partInfo.u32Offset_),
                                                  std::vector<uint8_t>(&reinterpret_cast<const char *>(buf)[payloadIndex],
                                                                       &reinterpret_cast<const char *>(buf)[payloadIndex + partInfo.u32Size_])));

                      // determine if all parts are accounted for
                      size_t totalExpectedPartsBytes = totalDataBytes + totalEventBytes + totalControlBytes;

                      if(totalReceivedPartsBytes  == totalExpectedPartsBytes)
                        {
                          Utils::VectorIO vectorIO{};

                          // get the parts sorted by offset and build an iovec
                          for(const auto & part : parts)
                            {
                              vectorIO.push_back({const_cast<uint8_t *>(part.second.data()),
                                  part.second.size()});
                            }

                          handleOTAMessage(now,
                                           totalEventBytes,
                                           totalControlBytes,
                                           totalDataBytes,
                                           otaHeader.source(),
                                           otaHeader.destination(),
                                           otaHeader.sequence(),
                                           vectorIO,
                                           records);

                          // remove part cache and part time store
                          partStore_.erase(iter);
                        }
                    }
                  else
                    {
                      PartKey partKey = PartKey{otaHeader.source(),otaHeader.sequence()};

                      Parts parts{};

                      parts.insert(std::make_pair(static_cast<size_t>(partInfo.u32Offset_),
                                                  std::vector<uint8_t>(&reinterpret_cast<const char *>(buf)[payloadIndex],
                                                                       &reinterpret_cast<const char *>(buf)[payloadIndex + partInfo.u32Size_])));

                      // first part of message
                      // check to see if first part has been received
                      if(otaHeader.has_payloadinfo())
                        {
                          auto & payloadInfo = otaHeader.payloadinfo();

                          partStore_.insert({partKey,
                              std::make_tuple(static_cast<size_t>(partInfo.u32Size_),
                                              payloadInfo.eventlength(),
                                              payloadInfo.controllength(),
                                              payloadInfo.datalength(),
                                              parts,
                                              now)});
                        }
                      else
                        {
                          partStore_.insert({partKey,
                              std::make_tuple(static_cast<size_t>(partInfo.u32Size_),
                                              0, // event length
                                              0, // control length
                                              0, // data length
                                              parts,
                                              now)});
                        }
                    }
                }
            }
          else
            {
              LOGGER_STANDARD_LOGGING(*LogServiceSingleton::instance(),
                                      ERROR_LEVEL,
                                      "OTAExtractor message part size mismatch");
            }
        }
      else
        {
          LOGGER_STANDARD_LOGGING(*LogServiceSingleton::instance(),
                                  ERROR_LEVEL,
                                  "OTAExtractor message header could not be deserialized");
        }
    }
  else
    {
      LOGGER_STANDARD_LOGGING(*LogServiceSingleton::instance(),
                              ERROR_LEVEL,
                              "OTAExtractor message missing header missing prefix length encoding");
    }

  // check to see if there are part assemblies to abandon
  if(lastPartCheckTime_ + partCheckThreshold_ <= now)
    {
      for(auto iter = partStore_.begin(); iter != partStore_.end();)
        {
          auto & lastPartTime = std::get<5>(iter->second);

          if(lastPartTime + partTimeoutThreshold_ <= now)
            {
              partStore_.erase(iter++);
            }
          else
            {
              ++iter;
            }
        }

      lastPartCheckTime_ = now;
    }

  return records;
}

void
EMANE::SpectrumTools::Recorder::OTAExtractor::handleOTAMessage(const TimePoint & now,
                                                               size_t eventsSize,
                                                               size_t controlsSize,
                                                               size_t dataSize,
                                                               NEMId source,
                                                               NEMId destination,
                                                               std::uint64_t u64SequenceNumber,
                                                               const Utils::VectorIO & vectorIO,
                                                               Records & records)
{
  size_t index{};
  size_t offset{};

  if(eventsSize)
    {
      EMANESpectrumTools::Record record{};
      record.set_type(EMANESpectrumTools::Record::TYPE_EVENT);
      record.set_timestamp(std::chrono::duration_cast<EMANE::Microseconds>(now.time_since_epoch()).count());

      auto pEvent = record.mutable_event();

      pEvent->set_event_data(stringFromVectorIO(eventsSize,
                                                index,
                                                offset,
                                                vectorIO));

      records.emplace_back(std::move(record));
    }

  // advance over any ota control
  if(controlsSize)
    {
      stringFromVectorIO(controlsSize,
                         index,
                         offset,
                         vectorIO);
    }

  if(dataSize > 2)
    {
      dataSize -= 2;

      // get the common phy header
      std::string sLength{stringFromVectorIO(2,
                                             index,
                                             offset,
                                             vectorIO)};

      std::uint16_t u16Length = NTOHS(*reinterpret_cast<const std::uint16_t *>(sLength.c_str()));

      if(dataSize >= static_cast<size_t>(u16Length))
        {
          dataSize -= u16Length;

          EMANESpectrumTools::Record record{};
          record.set_type(EMANESpectrumTools::Record::TYPE_OTA);
          record.set_timestamp(std::chrono::duration_cast<EMANE::Microseconds>(now.time_since_epoch()).count());

          auto pOTA = record.mutable_ota();

          pOTA->set_source(source);
          pOTA->set_destination(destination);
          pOTA->set_sequence(u64SequenceNumber);

          pOTA->set_common_phy_header(stringFromVectorIO(u16Length,
                                                         index,
                                                         offset,
                                                         vectorIO));

          if(dataSize > 2)
            {
              // get the common mac header
              sLength = stringFromVectorIO(2,
                                           index,
                                           offset,
                                           vectorIO);

              u16Length = NTOHS(*reinterpret_cast<const std::uint16_t *>(sLength.c_str()));

              if(dataSize >= static_cast<size_t>(u16Length))
                {
                  pOTA->set_common_mac_header(stringFromVectorIO(u16Length,
                                                                 index,
                                                                 offset,
                                                                 vectorIO));
                }
            }

          records.emplace_back(std::move(record));
        }
    }
}
