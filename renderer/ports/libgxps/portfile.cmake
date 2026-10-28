# Derived from microsoft/vcpkg at 866e9e258a76437074a5cf02d4c2d71b53bff2f8 (MIT).
vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)
vcpkg_download_distfile(ARCHIVE
    URLS "https://download.gnome.org/sources/libgxps/0.3/libgxps-0.3.2.tar.xz"
    FILENAME "GNOME-libgxps-0.3.2.tar.xz"
    SHA512 d3d56e4906477b68d088bf83bde666f9ea8bf383add592772aad53dd571e727f1bc0410dd020e12212ede5ff8e26cb46150a9860a6f7af29c4d195f03e946fe9
)
vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    PATCHES meson-compat.patch fail-closed.patch
)
vcpkg_configure_meson(SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Ddisable-introspection=true
        -Denable-test=false
        -Dwith-libjpeg=true
        -Dwith-liblcms2=true
        -Dwith-libtiff=true
)
vcpkg_install_meson()
vcpkg_fixup_pkgconfig()
# Tools are not shipped with the worker; keep upstream tools out of its runtime.
vcpkg_copy_tools(TOOL_NAMES xpstojpeg xpstopdf xpstopng xpstops xpstosvg AUTO_CLEAN)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
