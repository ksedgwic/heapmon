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

HELLO =		hello

HELLOSRC =	hello.cpp

LIBS =		-lpthread -ldl

all:		$(MODSO) $(HELLO)

clean:
	rm -f $(MODSO) $(MODOBJ) $(HELLO)

$(MODSO):	$(MODOBJ)
	$(SOCMD) $(SOFLAGS) -o $@ $(CPPFLAGS) $(LDFLAGS) $(MODOBJ) $(LIBS)

%.o:		%.cpp
	$(CPPCMD) -c $< -o $@ $(CPPFLAGS) $(DEFINES) $(INCS)

%:		%.cpp
	$(CPPCMD) $< -o $@ $(CPPFLAGS) $(DEFINES) $(INCS)
