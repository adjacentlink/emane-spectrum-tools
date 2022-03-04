/*
 * Copyright (c) 2022 - Adjacent Link LLC, Bridgewater, New Jersey
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

#include "otaextractor.h"
#include "eventextractor.h"
#include "multicastsocket.h"

#include <emane/application/logger.h>
#include <emane/utils/parameterconvert.h>
#include <emane/net.h>
#include <emane/exception.h>

#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <fstream>
#include <uuid.h>
#include <cstdlib>
#include <iostream>

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace
{
  const int DEFAULT_LOG_LEVEL{3};

  const int DEFAULT_PRIORITY_LEVEL{50};

  const char * DEFAULT_OTA_GROUP = "224.1.2.8:45702";

  const char * DEFAULT_OTA_DEVICE = "lo";

  const char * DEFAULT_EVENT_GROUP = "224.1.2.8:45703";

  const char * DEFAULT_EVENT_DEVICE = "lo";

  const uint64_t one{1};

  int iFd{};

  void sighandler(int)
  {
    write(iFd,&one,sizeof(one));
  }

  class  MulticastSocketPatch : public EMANE::MulticastSocket
  {
  public:
    int handle()
    {
      return iSock_;
    }
  };
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
          {"pidfile" , 1, nullptr,  1},
          {"uuidfile", 1, nullptr,  2},
          {"priority", 1, nullptr,  'p'},
          {"otamanagerdevice", 1, nullptr, 3},
          {"otamanagergroup", 1, nullptr, 4},
          {"eventservicedevice", 1, nullptr, 5},
          {"eventservicegroup", 1, nullptr, 6},
          {nullptr, 0, nullptr, 0},
        };

      int iOption{};
      int iOptionIndex{};
      bool bDaemonize{};
      bool bRealtime{};
      int  iLogLevel{DEFAULT_LOG_LEVEL};
      int  iPriority{DEFAULT_PRIORITY_LEVEL};
      std::string sLogFile{};
      std::string sPIDFile{};
      std::string sUUIDFile{};

      std::string sOTAGroup{DEFAULT_OTA_GROUP};
      std::string sOTADevice{DEFAULT_OTA_DEVICE};
      std::string sEventGroup{DEFAULT_EVENT_GROUP};
      std::string sEventDevice{DEFAULT_EVENT_DEVICE};

      while((iOption = getopt_long(argc,argv,"hrvdf:l:p:", &options[0],&iOptionIndex)) != -1)
        {
          switch(iOption)
            {
            case 'h':
              // --help
              std::cout<<"usage: emane-spectrum-ota-recorder [OPTIONS]... <record file>"<<std::endl;
              std::cout<<std::endl;
              std::cout<<"options:"<<std::endl;
              std::cout<<"  -d, --daemonize                Run in the background."<<std::endl;
              std::cout<<"  -h, --help                     Print this message and exit."<<std::endl;
              std::cout<<"  -f, --logfile FILE             Log to a file instead of stdout."<<std::endl;
              std::cout<<"  -l, --loglevel [0,4]           Set initial log level."<<std::endl;
              std::cout<<"                                  default: "<<DEFAULT_LOG_LEVEL<<std::endl;
              std::cout<<"  --pidfile FILE                 Write application pid to file."<<std::endl;
              std::cout<<"  -p, --priority [0,99]          Set realtime priority level."<<std::endl;
              std::cout<<"                                 Only used with -r, --realtime."<<std::endl;
              std::cout<<"                                  default: "<<DEFAULT_PRIORITY_LEVEL<<std::endl;
              std::cout<<"  -r, --realtime                 Set realtime scheduling."<<std::endl;
              std::cout<<"  --uuidfile FILE                Write the application instance UUID to file."<<std::endl;
              std::cout<<"  -v, --version                  Print version and exit."<<std::endl;
              std::cout<<std::endl;
              std::cout<<"EMANE emulator config:"<<std::endl;
              std::cout<<"  --eventservicedevice VALUE      default:"<<DEFAULT_EVENT_DEVICE<<" *"<<std::endl;
              std::cout<<"  --eventservicegroup VALUE       default:"<<DEFAULT_EVENT_GROUP<<" *"<<std::endl;
              std::cout<<"  --otamanagerdevice VALUE        default:"<<DEFAULT_OTA_DEVICE<<" *"<<std::endl;
              std::cout<<"  --otamanagergroup VALUE         default:"<<DEFAULT_OTA_GROUP<<" *"<<std::endl;
              std::cout<<std::endl;
              return EXIT_SUCCESS;
              break;

            case 1:
              sPIDFile = optarg;
              break;

            case 2:
              sUUIDFile = optarg;
              break;

            case 3:
              sOTADevice = optarg;
              break;

            case 4:
              sOTAGroup = optarg;
              break;

            case 5:
              sEventDevice = optarg;
              break;

            case 6:
              sEventGroup = optarg;
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

      if(optind >= argc)
        {
          std::cerr<<"Invalid number of parameters. See `emane-spectrum-ota-recorder --help`."<<std::endl;
          return EXIT_FAILURE;
        }

      std::string sRecordFile{argv[optind]};

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

      std::ofstream recorderFileStream{sRecordFile.c_str(), std::fstream::out | std::fstream::trunc};

      if(!recorderFileStream)
        {
          logger.log(EMANE::ABORT_LEVEL,"unable to open record file: %s",sRecordFile.c_str());
          return EXIT_FAILURE;
        }

      auto otaGroupAddress =
        EMANE::Utils::ParameterConvert{sOTAGroup}.toINETAddr();

      auto eventGroupAddress =
        EMANE::Utils::ParameterConvert{sEventGroup}.toINETAddr();

      // eventfd used to shutdown applicion
      iFd = eventfd(0,0);
      int iepollFd{epoll_create1(0)};

      // add the eventfd socket to the epoll instance
      struct epoll_event ev;
      memset(&ev,0,sizeof(epoll_event));
      ev.events = EPOLLIN;
      ev.data.fd = iFd;

      if(epoll_ctl(iepollFd,EPOLL_CTL_ADD,iFd,&ev) == -1)
        {
          logger.log(EMANE::ABORT_LEVEL,"unable to add eventfd to epoll");

          return EXIT_FAILURE;
        }

      // OTA multicast channel
      MulticastSocketPatch mcastOTA_{};
      mcastOTA_.open(otaGroupAddress,true,sOTADevice);

      memset(&ev,0,sizeof(epoll_event));
      ev.events = EPOLLIN;
      ev.data.fd = mcastOTA_.handle();

      // add the OTA multicast socket to the epoll instance
      if(epoll_ctl(iepollFd,EPOLL_CTL_ADD,mcastOTA_.handle(),&ev) == -1)
        {
          logger.log(EMANE::ABORT_LEVEL,"unable to add ota fd to epoll");

          return EXIT_FAILURE;
        }

      // Event multicast channel
      MulticastSocketPatch mcastEvent_{};
      mcastEvent_.open(eventGroupAddress,true,sEventDevice);

      memset(&ev,0,sizeof(epoll_event));
      ev.events = EPOLLIN;
      ev.data.fd = mcastEvent_.handle();

      // add the ota multicast socket to the epoll instance
      if(epoll_ctl(iepollFd,EPOLL_CTL_ADD,mcastEvent_.handle(),&ev) == -1)
        {
          logger.log(EMANE::ABORT_LEVEL,"unable to add event fd to epoll");

          return EXIT_FAILURE;
        }

      // set signal handler
      struct sigaction action;
      memset(&action,0,sizeof(action));
      action.sa_handler = sighandler;
      sigaction(SIGINT,&action,nullptr);
      sigaction(SIGQUIT,&action,nullptr);

#define MAX_EVENTS 32
      struct epoll_event events[32];
      std::uint64_t u64Expired{};
      int nfds{};
      bool bRun{true};
      char buf[65535];

      EMANE::SpectrumTools::Recorder::OTAExtractor otaExtractor{EMANE::Seconds{2},EMANE::Seconds{5}};
      EMANE::SpectrumTools::Recorder::EventExtractor eventExtractor{};

      while(bRun)
        {
          nfds = epoll_wait(iepollFd,events,MAX_EVENTS,-1);

          if(nfds == -1)
            {
              if(errno == EINTR)
                {
                  continue;
                }

              break;
            }

          EMANE::SpectrumTools::Recorder::Records records{};

          for(int n = 0; n < nfds; ++n)
            {
              if(events[n].data.fd == iFd)
                {
                  if(read(iFd,&u64Expired,sizeof(u64Expired)) > 0)
                    {
                      bRun = false;
                      break;
                    }
                }
              else if(events[n].data.fd == mcastEvent_.handle())
                {
                  auto len = mcastEvent_.recv(buf,sizeof(buf));

                  if(len > 0)
                    {
                      records.splice(records.end(),eventExtractor.process(buf,len));
                    }
                }
              else if(events[n].data.fd ==  mcastOTA_.handle())
                {
                  auto len = mcastOTA_.recv(buf,sizeof(buf));

                  if(len > 0)
                    {
                      records.splice(records.end(),otaExtractor.process(buf,len));
                    }
                }
            }

          for(auto & record : records)
            {
              std::string sSerialization{};

              if(record.SerializeToString(&sSerialization))
                {
                  std::uint32_t u32MessageFrameLength = htonl(sSerialization.length());

                  recorderFileStream.write(reinterpret_cast<char *>(&u32MessageFrameLength),
                                           sizeof(u32MessageFrameLength));

                  recorderFileStream.write(sSerialization.c_str(),sSerialization.length());
                }
            }
        }

      recorderFileStream.close();

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
