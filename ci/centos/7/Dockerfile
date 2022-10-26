FROM centos:centos7

RUN mkdir -p /opt/built

RUN yum -y install epel-release
RUN yum -y update

# Copy and install packages from the latest opentestpoint build
WORKDIR /opt
RUN mkdir -p dependencies
COPY --from=gcr.io/automated-builds-303920/centos7.emane /opt/built/* dependencies/
RUN yum -y install dependencies/*\.rpm

# Build tools
RUN yum -y install git gcc-c++ make autoconf automake libtool rpm-build python-setuptools \
                   libxml2-devel libpcap-devel pcre-devel libuuid-devel python-devel \
                   protobuf protobuf-devel protobuf-compiler protobuf-python zeromq-devel

# Setup emane_SRC_ROOT environment variable and build protobuf files
RUN git clone https://github.com/adjacentlink/emane -b develop
RUN cd emane && ./autogen.sh && ./configure && make
ENV emane_SRC_ROOT=/opt/emane

RUN git clone https://github.com/adjacentlink/emane-spectrum-tools -b develop
WORKDIR emane-spectrum-tools
RUN ./autogen.sh && ./configure && make rpm
RUN cp $(find .rpmbuild/RPMS -name "*\.rpm") /opt/built
RUN yum -y install /opt/built/*

RUN yum clean all

RUN echo 'complete'

