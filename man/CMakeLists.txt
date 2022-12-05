find_program(XSLTPROC xsltproc)
if(XSLTPROC)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/firebuild.1.xml.in ${CMAKE_CURRENT_BINARY_DIR}/firebuild.1.xml @ONLY)
  add_custom_target(man
    ${XSLTPROC} --nonet --param "man.authors.section.enabled" "0" --param "man.output.in.separate.dir" "1" http://docbook.sourceforge.net/release/xsl/current/manpages/profile-docbook.xsl ${CMAKE_CURRENT_BINARY_DIR}/firebuild.1.xml
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/firebuild.1.xml
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating man pages with xsltproc" VERBATIM)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/man1/firebuild.1" DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
  add_dependencies(firebuild man)
endif(XSLTPROC)


