CPPCMD = 	g++
CPPFLAGS =	-fPIC -g -Wall -Werror -O3
DEFINES +=	-DLINUX -D_REENTRANT
SOCMD =		g++
SOFLAGS =	-shared

MODSO =		heapmon.so

MODSRC = 	\
			heapmon.cpp \
			$(NULL)

MODOBJ = 	$(MODSRC:%.cpp=%.o)

LIBS =		-lpthread -ldl

all:		$(MODSO)

clean:
	rm -f $(MODSO) $(MODOBJ)

$(MODSO):	$(MODOBJ)
	$(SOCMD) $(SOFLAGS) -o $@ $(CPPFLAGS) $(LDFLAGS) $(MODOBJ) $(LIBS)

%.o:		%.cpp
	$(CPPCMD) -c $< -o $@ $(CPPFLAGS) $(DEFINES) $(INCS)
