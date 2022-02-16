/*
 * Copyright (c) 2018-2021 - Adjacent Link LLC, Bridgewater, New Jersey
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

#include "monitorconfigurationfile.h"

#include <cstdlib>
#include <iostream>

#include <emane/application/logger.h>
#include <emane/application/nembuilder.h>
#include <emane/utils/parameterconvert.h>
#include <emane/exception.h>

#include <mutex>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <fstream>
#include <map>

namespace
{
  const int DEFAULT_LOG_LEVEL{3};

  const int DEFAULT_PRIORITY_LEVEL{50};

  const char * DEFAULT_CONTROL_PORT_ENDPOINT = "0.0.0.0:57000";

  const char * DEFAULT_EVENTSERVICE_GROUP = "224.1.2.8:45703";

  const char * DEFAULT_EVENTSERVICE_GROUP_DEVICE = "lo";

  const char * DEFAULT_OTA_GROUP = "224.1.2.8:45702";

  const char * DEFAULT_OTA_GROUP_DEVICE = "lo";

  std::mutex mutex{};

  void sighandler(int)
  {
    mutex.unlock();
  }
}

int main(int argc, char* argv[])
{
  try
    {
      std::vector<option> options =
        {
          {"help", 0, nullptr, 'h'},
          {"realtime", 0, nullptr, 'r'},
          {"loglevel", 1, nullptr, 'l'},
          {"logfile",  1, nullptr, 'f'},
          {"daemonize",0, nullptr, 'd'},
          {"version" , 0, nullptr, 'v'},
          {"pidfile" , 1, nullptr,  3},
          {"uuidfile", 1, nullptr,  4},
          {"priority", 1, nullptr,  'p'},
          {"config", 1, nullptr,'c'},
          {"nem", 1, nullptr,'n'},
          {"bandwidth", 1, nullptr, 1},
          {"fading.model", 1, nullptr, 1},
          {"fading.nakagami.distance0", 1, nullptr, 1},
          {"fading.nakagami.distance1", 1, nullptr, 1},
          {"fading.nakagami.m0", 1, nullptr, 1},
          {"fading.nakagami.m1", 1, nullptr, 1},
          {"fading.nakagami.m2", 1, nullptr, 1},
          {"fixedantennagain", 1, nullptr, 1},
          {"fixedantennagainenable", 1, nullptr, 1},
          {"noisebinsize", 1, nullptr, 1},
          {"noisemaxclampenable", 1, nullptr, 1},
          {"noisemaxmessagepropagation", 1, nullptr, 1},
          {"noisemaxsegmentduration", 1, nullptr, 1},
          {"noisemaxsegmentoffset", 1, nullptr, 1},
          {"propagationmodel", 1, nullptr, 1},
          {"systemnoisefigure", 1, nullptr, 1},
          {"timesyncthreshold", 1, nullptr, 1},
          {"spectrumquery.binsize", 1, nullptr, 1},
          {"spectrumquery.rate", 1, nullptr, 1},
          {"spectrumquery.publishendpoint", 1, nullptr, 1},
          {"spectrumquery.recorderfile", 1, nullptr, 1},
          {"antennaprofilemanifesturi", 1, nullptr, 5},
          {"controlportendpoint", 1, nullptr, 5},
          {"eventservicedevice", 1, nullptr, 5},
          {"eventservicegroup", 1, nullptr, 5},
          {"eventservicettl", 1, nullptr, 5},
          {"otamanagerchannelenable", 1, nullptr, 5},
          {"otamanagerdevice", 1, nullptr, 5},
          {"otamanagergroup", 1, nullptr, 5},
          {"otamanagerloopback", 1, nullptr, 5},
          {"otamanagermtu", 1, nullptr, 5},
          {"otamanagerpartcheckthreshold", 1, nullptr, 5},
          {"otamanagerparttimeoutthreshold", 1, nullptr, 5},
          {"otamanagerttl", 1, nullptr, 5},
          {"stats.event.maxeventcountrows", 1, nullptr, 5},
          {"stats.ota.maxeventcountrows", 1, nullptr, 5},
          {"stats.ota.maxpacketcountrows", 1, nullptr, 5},
          {"spectralmaskmanifesturi", 1, nullptr, 5},
          {nullptr, 0, nullptr, 0},
        };

      int iOption{};
      int iOptionIndex{};
      EMANE::NEMId id{};
      bool bDaemonize{};
      bool bRealtime{};
      int  iLogLevel{DEFAULT_LOG_LEVEL};
      int  iPriority{DEFAULT_PRIORITY_LEVEL};
      std::string sLogFile{};
      std::string sPIDFile{};
      std::string sUUIDFile{};
      std::string sControlPortEndpoint{DEFAULT_CONTROL_PORT_ENDPOINT};
      std::string sOTAGroup{DEFAULT_OTA_GROUP};
      std::string sOTAGroupDevice{DEFAULT_OTA_GROUP_DEVICE};
      std::string sEventServiceGroup{DEFAULT_EVENTSERVICE_GROUP};
      std::string sEventServiceGroupDevice{DEFAULT_EVENTSERVICE_GROUP_DEVICE};

      EMANE::SpectrumTools::MonitorConfigurationFile configurationFile{};

      EMANE::SpectrumTools::MonitorConfigurationFile::ConfigurationMap phyConfigArgs{};
      EMANE::SpectrumTools::MonitorConfigurationFile::ConfigurationMap emuConfigArgs{};

      while((iOption = getopt_long(argc,argv,"hrvdf:l:p:c:n:", &options[0],&iOptionIndex)) != -1)
        {
          switch(iOption)
            {
            case 'h':
              // --help
              std::cout<<"usage: emane-spectrum-monitor [OPTIONS]... "<<std::endl;
              std::cout<<std::endl;
              std::cout<<"options:"<<std::endl;
              std::cout<<"  -c, --config FILE              Read configuration from file."<<std::endl;
              std::cout<<"  -d, --daemonize                Run in the background."<<std::endl;
              std::cout<<"  -h, --help                     Print this message and exit."<<std::endl;
              std::cout<<"  -f, --logfile FILE             Log to a file instead of stdout."<<std::endl;
              std::cout<<"  -l, --loglevel [0,4]           Set initial log level."<<std::endl;
              std::cout<<"                                  default: "<<DEFAULT_LOG_LEVEL<<std::endl;
              std::cout<<"  -n,--nem NEMID                 NEM id of node.[1,65534]."<<std::endl;
              std::cout<<"  --pidfile FILE                 Write application pid to file."<<std::endl;
              std::cout<<"  -p, --priority [0,99]          Set realtime priority level."<<std::endl;
              std::cout<<"                                 Only used with -r, --realtime."<<std::endl;
              std::cout<<"                                  default: "<<DEFAULT_PRIORITY_LEVEL<<std::endl;
              std::cout<<"  -r, --realtime                 Set realtime scheduling."<<std::endl;
              std::cout<<"  --uuidfile FILE                Write the application instance UUID to file."<<std::endl;
              std::cout<<"  -v, --version                  Print version and exit."<<std::endl;
              std::cout<<std::endl;
              std::cout<<"EMANE emulator config:"<<std::endl;
              std::cout<<"  --antennaprofilemanifesturi VALUE"<<std::endl;
              std::cout<<"  --controlportendpoint VALUE     default:"<<DEFAULT_CONTROL_PORT_ENDPOINT<<" *"<<std::endl;
              std::cout<<"  --eventservicedevice VALUE      default:"<<DEFAULT_EVENTSERVICE_GROUP_DEVICE<<" *"<<std::endl;
              std::cout<<"  --eventservicegroup VALUE       default:"<<DEFAULT_EVENTSERVICE_GROUP<<" *"<<std::endl;
              std::cout<<"  --eventservicettl VALUE **"<<std::endl;
              std::cout<<"  --otamanagerchannelenable VALUE **"<<std::endl;
              std::cout<<"  --otamanagerdevice VALUE        default:"<<DEFAULT_OTA_GROUP_DEVICE<<" *"<<std::endl;
              std::cout<<"  --otamanagergroup VALUE         default:"<<DEFAULT_OTA_GROUP<<" *"<<std::endl;
              std::cout<<"  --otamanagerloopback VALUE **"<<std::endl;
              std::cout<<"  --otamanagermtu VALUE **"<<std::endl;
              std::cout<<"  --otamanagerpartcheckthreshold VALUE **"<<std::endl;
              std::cout<<"  --otamanagerparttimeoutthreshold VALUE **"<<std::endl;
              std::cout<<"  --otamanagerttl VALUE **"<<std::endl;
              std::cout<<"  --spectralmaskmanifesturi VALUE"<<std::endl;
              std::cout<<"  --stats.event.maxeventcountrows VALUE **"<<std::endl;
              std::cout<<"  --stats.ota.maxeventcountrows VALUE **"<<std::endl;
              std::cout<<"  --stats.ota.maxpacketcountrows VALUE **"<<std::endl;
              std::cout<<std::endl;
              std::cout<<"See https://github.com/adjacentlink/emane/wiki/Configuring-the-Emulator"<<std::endl;
              std::cout<<"Place emulator parameters in </emane-spectrum-monitor/emulator>."<<std::endl;
              std::cout<<std::endl;
              std::cout<<"EMANE monitor config:"<<std::endl;
              std::cout<<"  --spectrumquery.binsize VALUE   default: 10000 microseconds"<<std::endl;
              std::cout<<"  --spectrumquery.publishendpoint VALUE default: 0.0.0.0:8883"<<std::endl;
              std::cout<<"  --spectrumquery.rate VALUE      default: 100000 microseconds"<<std::endl;
              std::cout<<"  --spectrumquery.recorderfile VALUE  optional"<<std::endl;
              std::cout<<std::endl;
              std::cout<<"EMANE Physical Layer config:"<<std::endl;
              std::cout<<"  --bandwidth VALUE **"<<std::endl;
              std::cout<<"  --fading.model VALUE **"<<std::endl;
              std::cout<<"  --fading.nakagami.distance0 **"<<std::endl;
              std::cout<<"  --fading.nakagami.distance1 **"<<std::endl;
              std::cout<<"  --fading.nakagami.m0 VALUE **"<<std::endl;
              std::cout<<"  --fading.nakagami.m1 VALUE **"<<std::endl;
              std::cout<<"  --fading.nakagami.m2 VALUE **"<<std::endl;
              std::cout<<"  --fixedantennagain VALUE **"<<std::endl;
              std::cout<<"  --fixedantennagainenable VALUE **"<<std::endl;
              std::cout<<"  --noisemaxclampenable VALUE **"<<std::endl;
              std::cout<<"  --noisemaxmessagepropagation VALUE **"<<std::endl;
              std::cout<<"  --noisemaxsegmentduration VALUE **"<<std::endl;
              std::cout<<"  --noisemaxsegmentoffset VALUE **"<<std::endl;
              std::cout<<"  --propagationmodel VALUE **"<<std::endl;
              std::cout<<"  --systemnoisefigure VALUE **"<<std::endl;
              std::cout<<"  --timesyncthreshold VALUE **"<<std::endl;
              std::cout<<std::endl;
              std::cout<<"See https://github.com/adjacentlink/emane/wiki/Physical-Layer-Model"<<std::endl;
              std::cout<<"Place monitor and physical layer parameters in </emane-spectrum-monitor/physical-layer>."<<std::endl;
              std::cout<<std::endl;
              std::cout<<"* : Default given where none exists for the emulator or differs from emulator."<<std::endl;
              std::cout<<"**: Default same as emulator or emulator physical layer."<<std::endl;
              return EXIT_SUCCESS;
              break;

            case 1:
              phyConfigArgs.insert({options[iOptionIndex].name,{optarg}});
              break;

            case 3:
              sPIDFile = optarg;
              break;

            case 4:
              sUUIDFile = optarg;
              break;

            case 5:
              emuConfigArgs.insert({options[iOptionIndex].name,{optarg}});
              break;

            case 'r':
              bRealtime = true;
              break;

            case 'd':
              bDaemonize = true;
              break;

            case 'v':
              std::cout<<VERSION<<std::endl;
              return EXIT_SUCCESS;

            case 'f':
              sLogFile = optarg;
              break;

            case 'p':
              try
                {
                  iPriority = EMANE::Utils::ParameterConvert{optarg}.toINT32(0,99);
                }
              catch(...)
                {
                  std::cerr<<"invalid priority: "<<optarg<<std::endl;
                  return EXIT_FAILURE;
                }

              break;

            case 'l':
              try
                {
                  iLogLevel = EMANE::Utils::ParameterConvert{optarg}.toINT32(0,4);
                }
              catch(...)
                {
                  std::cerr<<"invalid log level: "<<optarg<<std::endl;
                  return EXIT_FAILURE;
                }

              break;

            case 'c':
              configurationFile.load(optarg);
              break;

            case 'n':
              try
                {
                  id = EMANE::Utils::ParameterConvert{optarg}.toUINT16(1,65534);
                }
              catch(...)
                {
                  std::cerr<<"invalid NEM id: "<<optarg<<std::endl;
                  return EXIT_FAILURE;
                }

              break;
            case '?':
              if(optopt == 't')
                {
                  std::cerr<<"option -"<<static_cast<char>(optopt)<<" requires an argument."<<std::endl;
                }
              else if(isprint(optopt))
                {
                  std::cerr<<"unknown option -"<<static_cast<char>(optopt)<<"."<<std::endl;
                }
              else
                {
                  std::cerr<<"bad option"<<std::endl;
                }
              return EXIT_FAILURE;
            }
        }

      if(optind != argc)
        {
          std::cerr<<"Error too many parameters. See `emane-spectrum-monitor --help`."<<std::endl;
          return EXIT_FAILURE;
        }

      if(!id && configurationFile.id())
        {
          id = configurationFile.id();
        }

      if(!id)
        {
          std::cerr<<"Error use --nem to specify NEMID if not using --config FILE."<<std::endl;
          return EXIT_FAILURE;
        }

      if(bDaemonize)
        {
          if(sLogFile.empty() && iLogLevel != 0)
            {
              std::cerr<<"unable to daemonize log level must be 0 when logging to stdout"<<std::endl;;
              return EXIT_FAILURE;
            }

          if(daemon(1,0))
            {
              std::cerr<<"unable to daemonize"<<std::endl;
              return EXIT_FAILURE;
            }
        }

      // create and EMANE logger and set the initial level
      EMANE::Application::Logger logger;

      if(!sLogFile.empty())
        {
          logger.redirectLogsToFile(sLogFile.c_str());
        }

      if(iLogLevel > 0)
        {
          logger.setLogLevel(static_cast<EMANE::LogLevel>(iLogLevel));

          logger.open();
        }
      else
        {
          logger.setLogLevel(EMANE::NOLOG_LEVEL);
        }

      std::stringstream ss;
      for(int i = 0; i < argc; ++i)
        {
          ss<<argv[i]<<" ";
        }

      // create application instance UUID
      uuid_t uuid;
      uuid_generate(uuid);

      char uuidBuf[37];
      uuid_unparse(uuid,uuidBuf);

      if(bRealtime)
        {
          struct sched_param schedParam{iPriority};

          if(sched_setscheduler(0,SCHED_RR,&schedParam))
            {
              if(!sLogFile.empty() || !iLogLevel)
                {
                  std::cerr<<"unable to set realtime scheduler"<<std::endl;
                }

              logger.log(EMANE::ABORT_LEVEL,"unable to set realtime scheduler");

              return EXIT_FAILURE;
            }
        }
      else
        {
          if(!sLogFile.empty() || iLogLevel < 2)
            {
              std::cerr<<"Please consider using the realtime scheduler to improve fidelity."<<std::endl;
            }

          logger.log(EMANE::ERROR_LEVEL,"Please consider using the realtime scheduler to improve fidelity.");
        }


      logger.log(EMANE::INFO_LEVEL,"application: %s",ss.str().c_str());
      logger.log(EMANE::INFO_LEVEL,"application uuid: %s",uuidBuf);

      // create configuration for physical layer model
      auto phyConfig = configurationFile.physicalLayerConfiguration();

      for(const auto & item : phyConfig)
        {
          // note: failure to insert if parameter is already preset is
          // ok. command line takes precedence over file contents.
          phyConfigArgs.insert(item);
        }

      EMANE::ConfigurationUpdateRequest updateRequest{};

      for(const auto & item : phyConfigArgs)
        {
          updateRequest.push_back({item.first,item.second});
        }


      // create an NEM builder
      EMANE::Application::NEMBuilder nemBuilder{};

      // create an NEMLayers instance to hold the layers of yoru NEM
      EMANE::Application::NEMLayers layers{};

      // create and add to layers an emulator physical layer instance
      layers.push_back(nemBuilder.buildPHYLayer(id,
                                                "emane-spectrum-monitor",
                                                updateRequest,
                                                false));

      // create a list of all NEMs in your application (there should only be one)
      EMANE::Application::NEMs nems{};

      nems.push_back(nemBuilder.buildNEM(id,
                                         layers,
                                         {},
                                         false));

      // create configuration for the emulator
      auto emuConfig = configurationFile.emualtorConfiguration();

      for(const auto & item : emuConfig)
        {
          // note: failure to insert if parameter is already preset is
          // ok. command line takes precedence over file contents.
          emuConfigArgs.insert(item);
        }

      // ok if there insererst fail.
      emuConfigArgs.insert({"controlportendpoint",{sControlPortEndpoint}});
      emuConfigArgs.insert({"eventservicegroup",{sEventServiceGroup}});
      emuConfigArgs.insert({"otamanagergroup",{sOTAGroup}});
      emuConfigArgs.insert({"eventservicedevice",{sEventServiceGroupDevice}});
      emuConfigArgs.insert({"otamanagerdevice",{sOTAGroupDevice}});

      updateRequest.clear();

      for(const auto & item : emuConfigArgs)
        {
          updateRequest.push_back({item.first,item.second});
        }

      // create an NEM manager to control the emulator state
      // transitions and specifiy all the configuration traditionally
      // part of the platform XML
      auto pNEMManager =
        nemBuilder.buildNEMManager(uuid,
                                   nems,
                                   updateRequest);

      if(!sPIDFile.empty())
        {
          std::ofstream pidFile{sPIDFile.c_str(), std::ios::out};

          if(pidFile)
            {
              pidFile<<getpid()<<std::endl;
            }
          else
            {
              return EXIT_FAILURE;
            }
        }

      if(!sUUIDFile.empty())
        {
          std::ofstream uuidFile{sUUIDFile.c_str(), std::ios::out};

          if(uuidFile)
            {
              uuidFile<<uuidBuf<<std::endl;
            }
          else
            {
              return EXIT_FAILURE;
            }
        }


      // start the NEM manager
      pNEMManager->start();

      // post-start the NEM manager
      pNEMManager->postStart();

      struct sigaction action;

      memset(&action,0,sizeof(action));

      action.sa_handler = sighandler;

      sigaction(SIGINT,&action,nullptr);
      sigaction(SIGQUIT,&action,nullptr);

      mutex.lock();

      // signal handler unlocks mutex
      mutex.lock();

      //  wait here...

      // stop the  NEM manager
      pNEMManager->stop();

      // destroy the NEM manager
      pNEMManager->destroy();

      std::cout<<"fin"<<std::endl;
    }
  catch(std::exception & exp)
    {
      std::cerr<<"error: "
               <<exp.what()
               <<std::endl;
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
