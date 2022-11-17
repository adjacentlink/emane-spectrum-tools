FROM rockylinux:8

RUN mkdir -p /opt/built

RUN dnf -y update
RUN dnf -y install epel-release dnf-plugins-core
RUN dnf config-manager --set-enabled powertools

# Copy and install packages from the latest upsteam dependencies
WORKDIR /opt
RUN mkdir -p dependencies
COPY --from=gcr.io/automated-builds-303920/rockylinux8.emane /opt/built/* dependencies/
RUN dnf -y install dependencies/*\.rpm

# Build tools and depends
RUN dnf -y install git gcc-c++ make autoconf automake libtool rpm-build python3-setuptools \
                   libxml2-devel libpcap-devel pcre-devel libuuid-devel python3-devel \
                   python3-protobuf protobuf-devel zeromq-devel

# Setup emane_SRC_ROOT environment variable and build protobuf files
RUN git clone https://github.com/adjacentlink/emane -b develop
RUN cd emane && ./autogen.sh && ./configure && make
ENV emane_SRC_ROOT=/opt/emane

RUN git clone https://github.com/adjacentlink/emane-spectrum-tools -b develop

WORKDIR emane-spectrum-tools
RUN ./autogen.sh && ./configure && make rpm
RUN cp $(find .rpmbuild/RPMS -name "*\.rpm") /opt/built
RUN dnf -y install /opt/built/*

RUN dnf clean all

RUN echo 'complete'

