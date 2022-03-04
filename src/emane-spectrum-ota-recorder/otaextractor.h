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

#ifndef EMANE_SPECTRUMTOOL_RECORDER_OTAEXTRACTOR_
#define EMANE_SPECTRUMTOOL_RECORDER_OTAEXTRACTOR_

#include "records.h"

#include <emane/utils/vectorio.h>
#include <emane/types.h>

#include <map>
#include <tuple>

namespace EMANE
{
  namespace SpectrumTools
  {
    namespace Recorder
    {
      class OTAExtractor
      {
      public:
        OTAExtractor(const Seconds & partCheckThreshold,
                     const Seconds & partTimeoutThreshold);

        Records process(const void * buf, size_t len);

      private:
        Seconds partCheckThreshold_;
        Seconds partTimeoutThreshold_;

        using PartKey = std::tuple<NEMId, // source NEM
                                   std::uint64_t>; // sequence number

        using Parts = std::map<size_t, // offset
                               std::vector<std::uint8_t>>;

        using PartsData = std::tuple<size_t, // total size in map
                                     size_t, // events size bytes
                                     size_t, // controls size bytes
                                     size_t, // data size bytes
                                     Parts, // parts map
                                     TimePoint>; // last part time

        using PartStore = std::map<PartKey,PartsData>;
        PartStore partStore_;
        TimePoint lastPartCheckTime_;


        void handleOTAMessage(const TimePoint & now,
                              size_t eventsSize,
                              size_t controlsSize,
                              size_t dataSize,
                              NEMId source,
                              NEMId destination,
                              std::uint64_t u64SequenceNumber,
                              const Utils::VectorIO & vectorIO,
                              Records & records);
      };
    }
  }
}

#endif // EMANE_SPECTRUMTOOL_RECORDER_OTAEXTRACTOR_
