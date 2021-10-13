lib_LTLIBRARIES = libemane-spectrum-monitor.la

libemane_spectrum_monitor_la_CPPFLAGS= \
 $(libemane_CFLAGS) \
 $(libzmq_CFLAGS) \
 -I$(emane_SRC_ROOT)/src/libemane

libemane_spectrum_monitor_la_SOURCES = \
 receiveprocessoralt.cc \
 receiveprocessoralt.h \
 spectrummonitoralt.cc \
 spectrummonitoralt.h \
 maxnoisebin.h \
 monitorphy.h \
 monitorphy.cc

EXTRA_DIST= \
 spectrummonitor.proto

nodist_libemane_spectrum_monitor_la_SOURCES= \
 spectrummonitor.pb.cc \
 spectrummonitor.pb.h


BUILT_SOURCES = \
 $(nodist_libemane_spectrum_monitor_la_SOURCES)

libemane_spectrum_monitor_la_LDFLAGS= \
 $(libemane_LIBS) \
 $(libzmq_LIBS) \
 -avoid-version

clean-local:
	rm -f $(BUILT_SOURCES)


spectrummonitor.pb.cc spectrummonitor.pb.h: spectrummonitor.proto
	protoc -I=. --cpp_out=. $<
