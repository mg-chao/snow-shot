qt_add_executable(snow-draw-engine-qt-demo
    main.cpp
    demo_window.cpp
    demo_window.h
)

target_link_libraries(snow-draw-engine-qt-demo PRIVATE
    snow_draw_engine_qt_canvas
)

set_target_properties(snow-draw-engine-qt-demo PROPERTIES
    WIN32_EXECUTABLE TRUE
)
