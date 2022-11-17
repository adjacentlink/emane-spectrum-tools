bin_PROGRAMS = emane-spectrum-ota-recorder

emane_spectrum_ota_recorder_CXXFLAGS = \
 $(libemane_CFLAGS) \
 -I$(emane_SRC_ROOT)/src/libemane

emane_spectrum_ota_recorder_SOURCES =  \
 eventextractor.cc \
 eventextractor.h \
 otaextractor.cc \
 otaextractor.h \
 recorder.cc \
 records.h

EXTRA_DIST= \
 record.proto

nodist_emane_spectrum_ota_recorder_SOURCES= \
 record.pb.cc \
 record.pb.h

BUILT_SOURCES = \
 $(nodist_emane_spectrum_ota_recorder_SOURCES)

emane_spectrum_ota_recorder_LDADD = \
 $(libemane_LIBS)

clean-local:
	rm -f $(BUILT_SOURCES)

record.pb.cc record.pb.h: record.proto
	protoc -I=. --cpp_out=. $<
