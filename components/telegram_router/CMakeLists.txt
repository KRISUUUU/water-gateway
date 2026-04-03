idf_component_register(
    SRCS "src/telegram_router.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common dedup_service wmbus_link
)
