
ifndef NAVISERVER
    NAVISERVER  = /usr/local/ns
endif

#
# Module name
#
MODNAME   =  nsdbtds
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
# Since FreeTDS 0.82 the libtds.so is neither built or installed so now we need paths to the static libraries, also libnsdb is required

MODLIBS  =  -L$(FREETDS_HOME)/lib -L$(FREETDS_HOME)/src/replacements/.libs/ -L$(FREETDS_HOME)/src/tds/.libs/ -ltds -lreplacements -lnsdb -lpq

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


