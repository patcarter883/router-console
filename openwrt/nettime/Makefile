include $(TOPDIR)/rules.mk

PKG_NAME:=nettime
PKG_VERSION:=1.1
PKG_RELEASE:=1

PKG_MAINTAINER:=Pat
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

# GStreamer core + net headers/libs in the target staging dir. These are the
# `pkg-config --cflags/--libs gstreamer-net-1.0` output (gstreamer-net pulls
# gstreamer-1.0, glib-2.0, gobject-2.0 plus the gio-unix/glib include dirs),
# kept explicit so the build does not depend on the host pkg-config wrapper's
# environment. Refresh with pkg-config if the gstreamer version changes.
GST_CFLAGS:= \
	-I$(STAGING_DIR)/usr/include/gstreamer-1.0 \
	-I$(STAGING_DIR)/usr/include/gio-unix-2.0 \
	-I$(STAGING_DIR)/usr/include \
	-I$(STAGING_DIR)/usr/include/glib-2.0 \
	-I$(STAGING_DIR)/usr/lib/glib-2.0/include
GST_LIBS:=-L$(STAGING_DIR)/usr/lib -lgstnet-1.0 -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0

define Package/nettime
  SECTION:=net
  CATEGORY:=Network
  TITLE:=GStreamer net time provider for the video LAN
  DEPENDS:=+libgst1net +glib2
endef

define Package/nettime/description
  Serves the router's CLOCK_REALTIME (UTC epoch) over the GStreamer net time
  protocol so unprivileged GStreamer-based nodes and custom transports can lock
  to the video LAN's time authority without root or privileged ports.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) $(GST_CFLAGS) -pthread \
		-o $(PKG_BUILD_DIR)/nettime $(PKG_BUILD_DIR)/net_time_provider.c \
		$(GST_LIBS) -pthread $(TARGET_LDFLAGS)
endef

define Package/nettime/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nettime $(1)/usr/bin/nettime
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/nettime.init $(1)/etc/init.d/nettime
endef

$(eval $(call BuildPackage,nettime))
