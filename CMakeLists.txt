set(srcs "src/si5351.c" )

idf_component_register( SRCS ${srcs}
                        INCLUDE_DIRS "include"
                        PRIV_INCLUDE_DIRS "private_include"
                        REQUIRES driver)
