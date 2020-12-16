/*
 * Copyright (c) 2014,2020 - Adjacent Link LLC, Bridgewater, New Jersey
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

#ifndef EMANE_SPECTRUMTOOLS_MAXBINNOISEHEADER_
#define EMANE_SPECTRUMTOOLS_MAXBINNOISEHEADER_

#include "emane/utils/spectrumwindowutils.h"
#include "emane/utils/conversionutils.h"
#include "emane/spectrumserviceexception.h"

#include <algorithm>

namespace EMANE
{
  namespace SpectrumTools
  {
    double maxNoiseBin(const SpectrumWindow & window,
                       const TimePoint & startTime,
                       const TimePoint & endTime)
    {
      const auto & noiseData = std::get<0>(window);
      const TimePoint & windowStartTime = std::get<1>(window);
      const Microseconds & binSize =  std::get<2>(window);

      Microseconds::rep windowStartBin{Utils::timepointToAbsoluteBin(windowStartTime,binSize,false)};

      std::size_t startIndex{};
      std::size_t endIndex{noiseData.size()-1};

      if(startTime < windowStartTime)
        {
          throw makeException<SpectrumServiceException>("max bin start time < window start time");
        }
      else
        {
          startIndex = Utils::timepointToAbsoluteBin(startTime,binSize,false) - windowStartBin;
        }

      if(endTime != TimePoint::min())
        {
          if(endTime < startTime)
            {
              throw makeException<SpectrumServiceException>("max bin end time < max bin start time");
            }
          else
            {
              endIndex = Utils::timepointToAbsoluteBin(endTime,binSize,true) - windowStartBin;
            }
        }

      if(endIndex < startIndex)
        {
          throw makeException<SpectrumServiceException>("max bin end index %zu < max bin start index %zu,"
                                                        " num bins %zu",
                                                        startIndex,
                                                        endIndex,
                                                        noiseData.size());
        }
      else if(endIndex >= noiseData.size() || startIndex >= noiseData.size())
        {
          throw makeException<SpectrumServiceException>("bin index out of range, start index %zu,"
                                                        " end index %zu, num bins %zu",
                                                        startIndex,
                                                        endIndex,
                                                        noiseData.size());
        }

      return *std::max_element(&noiseData[startIndex],&noiseData[endIndex]+1);
    }
  }
}

#endif //EMANE_SPECTRUMTOOLS_MAXBINNOISEHEADER_
