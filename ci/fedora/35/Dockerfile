FROM fedora:35

RUN mkdir -p /opt/built

RUN dnf -y update

# Copy and install packages from the latest opentestpoint build
WORKDIR /opt
RUN mkdir -p dependencies
COPY --from=gcr.io/automated-builds-303920/fedora35.emane /opt/built/* dependencies/
RUN dnf -y install dependencies/*\.rpm

# Build tools and depends
RUN dnf -y install git gcc-c++ make autoconf automake libtool rpm-build python3-setuptools \
                   libxml2-devel libpcap-devel pcre-devel libuuid-devel python3-devel \
                   python3-protobuf protobuf-devel zeromq-devel

# Setup emane_SRC_ROOT environment variable
RUN git clone https://github.com/adjacentlink/emane -b develop
ENV emane_SRC_ROOT=/opt/emane

RUN git clone https://github.com/adjacentlink/emane-spectrum-tools -b develop

WORKDIR emane-spectrum-tools
RUN ./autogen.sh
RUN ./configure
RUN make rpm
RUN cp $(find .rpmbuild/RPMS -name "*\.rpm") /opt/built
RUN dnf -y install /opt/built/*

RUN dnf clean all

RUN echo 'complete'

