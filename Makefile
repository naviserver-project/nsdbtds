
ifndef NAVISERVER
    NAVISERVER  = /usr/local/ns
endif

#
# Module name
#
MOD       =  nsdbtds.so

#
# Objects to build.
#
OBJS      =  nsdbtds.o

#
# Set this to be the installation prefix of your FreeTDS installation.
#
FREETDS_HOME = /usr/local/freetds

#
# Header files in THIS directory
#
HDRS     =

#
# Extra libraries
#
MODLIBS  =  -L$(FREETDS_HOME)/lib -ltds -lreplacements

#
# Compiler flags
#
CFLAGS   = -I$(FREETDS_HOME)/include -I/usr/include/freetds


include  $(NAVISERVER)/include/Makefile.module


ifneq (,$(findstring gcc,$(LDSO)))
	MODLIBS := -Wl,-R$(FREETDS_HOME)/lib $(MODLIBS)
else
	MODLIBS := -R$(FREETDS_HOME)/lib $(MODLIBS)
endif


