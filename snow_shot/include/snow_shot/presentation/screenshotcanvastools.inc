// Each drawing request has a palette signal, application and engine tools, and stable input IDs.
// Define SNOW_SHOT_CANVAS_TOOL before including.
SNOW_SHOT_CANVAS_TOOL(selectRequested, Select, Select, "select", "select")
SNOW_SHOT_CANVAS_TOOL(speechBubbleRequested, SpeechBubble, SpeechBubble, "speech_bubble",
                      "speech_bubble")
SNOW_SHOT_CANVAS_TOOL(shapeRequested, Shape, Shape, "shape", "rectangle")
SNOW_SHOT_CANVAS_TOOL(arrowRequested, Arrow, Arrow, "arrow", "arrow")
SNOW_SHOT_CANVAS_TOOL(lineRequested, Line, Line, "line", "line")
SNOW_SHOT_CANVAS_TOOL(distanceToolRequested, Distance, Distance, "distance", "distance")
SNOW_SHOT_CANVAS_TOOL(angleToolRequested, Angle, Angle, "angle", "angle")
SNOW_SHOT_CANVAS_TOOL(bracketToolRequested, Bracket, Bracket, "bracket", "bracket")
SNOW_SHOT_CANVAS_TOOL(brushRequested, Brush, FreeDraw, "brush", "freehand")
SNOW_SHOT_CANVAS_TOOL(highlightRequested, RectangleHighlight, RectangleHighlight, "",
                      "rectangle_highlight")
SNOW_SHOT_CANVAS_TOOL(brushHighlightRequested, BrushHighlight, PenHighlight, "highlight",
                      "pen_highlight")
SNOW_SHOT_CANVAS_TOOL(eraserRequested, Eraser, Eraser, "eraser", "eraser")
SNOW_SHOT_CANVAS_TOOL(rectangleEraserRequested, RectangleEraser, RectangleEraser, "",
                      "rectangle_eraser")
SNOW_SHOT_CANVAS_TOOL(brushEraserRequested, BrushEraser, BrushEraser, "", "brush_eraser")
SNOW_SHOT_CANVAS_TOOL(magnifierRequested, Magnifier, Magnifier, "magnifier", "magnifier")
SNOW_SHOT_CANVAS_TOOL(spotlightRequested, Spotlight, Spotlight, "spotlight", "spotlight")
SNOW_SHOT_CANVAS_TOOL(rectangleFilterRequested, RectangleFilter, RectangleFilter, "",
                      "rectangle_filter")
SNOW_SHOT_CANVAS_TOOL(autoFilterRequested, AutoFilter, AutoFilter, "", "auto_filter")
SNOW_SHOT_CANVAS_TOOL(brushFilterRequested, BrushFilter, PenFilter, "filter", "pen_filter")
SNOW_SHOT_CANVAS_TOOL(watermarkRequested, Watermark, Watermark, "watermark", "watermark")
SNOW_SHOT_CANVAS_TOOL(textRequested, Text, Text, "text", "text")
SNOW_SHOT_CANVAS_TOOL(sequenceNumberRequested, SequenceNumber, SerialNumber, "serial_number",
                      "serial_number")
