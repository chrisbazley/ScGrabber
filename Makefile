# Project:   SGfrontend


# Toolflags:
CCflags = -c -depend !Depend -IC: -throwback -DNDEBUG -DOLD_SCL_STUBS -fahi -apcs 3/32/fpe2/swst/fp/nofpr -memaccess -L22-S22-L41
#CCflags = -c -depend !Depend -IC: -throwback -DOLD_SCL_STUBS -DDEBUG_OUTPUT -fahi -apcs 3/32/fpe2/swst/fp/nofpr
C++flags = -c -depend !Depend -IC: -throwback
Linkflags = -aif -c++ -o $@
ObjAsmflags = -throwback -NoCache -depend !Depend
CMHGflags = 
LibFileflags = -c -o $@
Squeezeflags = -v -o $@
ASMflags = 


# Final targets:
@.!RunImage:   C:o.StubsG C:o.wimplib C:o.eventlib C:stubsg_o.CBLib \
        C:o.toolboxlib @.o.SGFrontEnd @.o.MakeConfig @.o.FEutils @.o.KeyNames \
        @.o.ConfigFile @.o.SetupDbox C:stubsg_o.ErrNotRec 
        Link $(Linkflags) C:o.StubsG C:o.wimplib C:o.eventlib \
        C:stubsg_o.CBLib C:o.toolboxlib @.o.SGFrontEnd @.o.MakeConfig @.o.FEutils \
        @.o.KeyNames @.o.ConfigFile @.o.SetupDbox C:stubsg_o.ErrNotRec
        Squeeze $(Squeezeflags) @.!RunImage

# User-editable dependencies:
@.h.ScrGrabberHdr:   @.cmhg.ScrGrabberHdr
        cmhg @.cmhg.ScrGrabberHdr -d @.h.ScrGrabberHdr

# Static dependencies:
@.o.SGFrontEnd:   @.c.SGFrontEnd
        cc $(ccflags) -o @.o.SGFrontEnd @.c.SGFrontEnd 
@.o.FEutils:   @.c.FEutils
        cc $(ccflags) -o @.o.FEutils @.c.FEutils 
@.o.KeyNames:   @.c.KeyNames
        cc $(ccflags) -o @.o.KeyNames @.c.KeyNames 
@.o.ConfigFile:   @.c.ConfigFile
        cc $(ccflags) -o @.o.ConfigFile @.c.ConfigFile 
@.o.MakeConfig:   @.c.MakeConfig
        cc $(ccflags) -o @.o.MakeConfig @.c.MakeConfig 
@.o.SetupDbox:   @.c.SetupDbox
        cc $(ccflags) -o @.o.SetupDbox @.c.SetupDbox 


# Dynamic dependencies:
