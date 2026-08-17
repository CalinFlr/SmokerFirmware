idf_component_register(
    SRCS "app_main.cpp"
    PRIV_REQUIRES log smoker_app smoker_platform
    REQUIRED_IDF_TARGETS esp32s3
)
