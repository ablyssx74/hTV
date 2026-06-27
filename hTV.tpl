name			$(GUI_TARGET)
version			$(VERSION)-1
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
	$(GUI_TARGET) = $(VERSION)-1	
}
requires {
	haiku
	libsdl2
	curl
}	
urls {
	"https://github.com/ablyssx74/hTV"
}
