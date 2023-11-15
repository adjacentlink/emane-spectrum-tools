FROM ubuntu:22.04

RUN mkdir -p /opt/built

# prevent failures due to interactive apt transactions
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get -y update
RUN apt-get -y upgrade --no-install-recommends

# Copy and install packages from the latest opentestpoint build
WORKDIR /opt
RUN mkdir -p dependencies
COPY --from=gcr.io/automated-builds-303920/ubuntu22.04.emane /opt/built/* dependencies/
COPY --from=gcr.io/automated-builds-303920/ubuntu22.04.opentestpoint /opt/built/* dependencies/
COPY --from=gcr.io/automated-builds-303920/ubuntu22.04.opentestpoint-probe-emane /opt/built/* dependencies/
RUN dpkg -i dependencies/*; apt-get -y -f install

# Build tools and dependencies
RUN apt-get -y install git gcc g++ autoconf automake libtool dh-python  debhelper python3-setuptools \
                       libxml2-dev libprotobuf-dev libpcap-dev libpcre3-dev uuid-dev pkg-config \
                       python3-protobuf protobuf-compiler \
                       python3-zmq libzmq5 libzmq3-dev

# Setup emane_SRC_ROOT environment variable and build protobuf files
RUN git clone https://github.com/adjacentlink/emane -b develop
RUN cd emane && ./autogen.sh && ./configure && make
ENV emane_SRC_ROOT=/opt/emane

RUN git clone https://github.com/adjacentlink/emane-spectrum-tools -b develop

WORKDIR emane-spectrum-tools
RUN ./autogen.sh && ./configure && make deb
RUN cp $(find .debbuild -name "*\.deb") /opt/built
RUN dpkg -i /opt/built/*; apt-get -y install -f

RUN echo 'complete'

