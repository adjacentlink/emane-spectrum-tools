FROM fedora:35

RUN mkdir -p /opt/built

RUN dnf -y update

# Copy and install packages from the latest opentestpoint build
WORKDIR /opt
RUN mkdir -p dependencies
COPY --from=gcr.io/automated-builds-303920/fedora35.emane /opt/built/* dependencies/
RUN dnf -y install dependencies/*\.rpm

# Build tools and depends
RUN dnf -y install git gcc-c++ make autoconf automake libtool rpm-build
RUN dnf -y install libxml2-devel libpcap-devel pcre-devel libuuid-devel

RUN git clone https://github.com/adjacentlink/emane-spectrum-tools -b develop

WORKDIR emane-spectrum-tools
RUN ./autogen.sh
RUN ./configure
RUN make rpm
RUN cp $(find .rpmbuild/RPMS -name "*\.rpm") /opt/built
RUN dnf -y install /opt/built/*

RUN dnf clean all

RUN echo 'complete'

