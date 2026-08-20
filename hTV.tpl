name			$(GUI_TARGET)
version			$(VERSION)-$(REVISION)
architecture	$(ARCH)
summary 		"hTV"
description 	"hTV Player - Haiku SDL2 Neubla Supported Video Player"
packager		"ablyss <hTV@epluribusunix.net>"
vendor			"epluribusunix.net Project"
licenses {
	"MIT"
}
copyrights {
	"$(YEAR) ablyss"
}
provides {
	$(GUI_TARGET) = $(VERSION)-$(REVISION)
}
requires {
	haiku
	libsdl2$(is32bit)
	curl$(is32bit)
}	
urls {
	"https://github.com/ablyssx74/hTV"
}
