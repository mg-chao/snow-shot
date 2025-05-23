'use client';

import { zIndexs } from '@/utils/zIndex';
import { CaptureStep, DrawContext, DrawState } from '../../types';
import { useCallback, useContext, useImperativeHandle, useMemo, useRef, useState } from 'react';
import { Flex, theme } from 'antd';
import React from 'react';
import { DragButton, DragButtonActionType } from './components/dragButton';
import { DrawToolbarContext } from './extra';
import { KeyEventKey } from './components/keyEventWrap/extra';
import { AppstoreOutlined, CloseOutlined, CopyOutlined, DragOutlined } from '@ant-design/icons';
import {
    ArrowIcon,
    ArrowSelectIcon,
    CircleIcon,
    DiamondIcon,
    EraserIcon,
    FixedIcon,
    LineIcon,
    MosaicIcon,
    OcrDetectIcon,
    PenIcon,
    RectIcon,
    SaveIcon,
    ScrollScreenshotIcon,
    TextIcon,
} from '@/components/icons';
import {
    CaptureEvent,
    CaptureEventPublisher,
    CaptureStepPublisher,
    DrawStatePublisher,
    ScreenshotTypePublisher,
} from '../../extra';
import { useStateSubscriber } from '@/hooks/useStateSubscriber';
import { createPublisher } from '@/hooks/useStatePublisher';
import { EnableKeyEventPublisher } from './components/keyEventWrap/extra';
import { HistoryControls } from './components/historyControls';
import { ToolButton } from './components/toolButton';
import { FormattedMessage, useIntl } from 'react-intl';
import { BlurTool } from './components/tools/blurTool';
import { ScreenshotType } from '@/functions/screenshot';
import { ScrollScreenshot } from './components/tools/scrollScreenshotTool';
import { AntdContext } from '@/components/globalLayoutExtra';

export type DrawToolbarProps = {
    actionRef: React.RefObject<DrawToolbarActionType | undefined>;
    onCancel: () => void;
    onSave: () => void;
    onFixed: () => void;
    onTopWindow: () => void;
    onCopyToClipboard: () => void;
    onOcrDetect: () => void;
};

export type DrawToolbarActionType = {
    setEnable: (enable: boolean) => void;
};

export const DrawToolbarStatePublisher = createPublisher<{
    mouseHover: boolean;
}>({
    mouseHover: false,
});

const DrawToolbarCore: React.FC<DrawToolbarProps> = ({
    actionRef,
    onCancel,
    onSave,
    onFixed,
    onCopyToClipboard,
    onTopWindow,
    onOcrDetect,
}) => {
    const { drawCacheLayerActionRef, selectLayerActionRef } = useContext(DrawContext);

    const [getDrawToolbarState, setDrawToolbarState] = useStateSubscriber(
        DrawToolbarStatePublisher,
        undefined,
    );
    const [getDrawState, setDrawState] = useStateSubscriber(DrawStatePublisher, undefined);
    const [, setCaptureStep] = useStateSubscriber(CaptureStepPublisher, undefined);
    const [getScreenshotType] = useStateSubscriber(ScreenshotTypePublisher, undefined);
    const { token } = theme.useToken();
    const { message } = useContext(AntdContext);
    const intl = useIntl();

    const enableRef = useRef(false);
    const [enableScrollScreenshot, setEnableScrollScreenshot] = useState(false);
    const drawToolarContainerRef = useRef<HTMLDivElement | null>(null);
    const drawToolbarRef = useRef<HTMLDivElement | null>(null);
    const dragButtonActionRef = useRef<DragButtonActionType | undefined>(undefined);
    const [, setEnableKeyEvent] = useStateSubscriber(EnableKeyEventPublisher, undefined);
    const draggingRef = useRef(false);

    const updateEnableKeyEvent = useCallback(() => {
        setEnableKeyEvent(enableRef.current && !draggingRef.current);
    }, [setEnableKeyEvent]);

    const onDraggingChange = useCallback(
        (dragging: boolean) => {
            draggingRef.current = dragging;
            updateEnableKeyEvent();
        },
        [updateEnableKeyEvent],
    );

    const setDragging = useCallback(
        (dragging: boolean) => {
            if (draggingRef.current === dragging) {
                return;
            }

            onDraggingChange(dragging);
        },
        [onDraggingChange],
    );

    const onToolClick = useCallback(
        (drawState: DrawState) => {
            const prev = getDrawState();

            if (drawState === DrawState.ScrollScreenshot) {
                const selectRect = selectLayerActionRef.current?.getSelectRect();
                if (
                    !selectRect ||
                    Math.min(
                        selectRect.max_x - selectRect.min_x,
                        selectRect.max_y - selectRect.min_y,
                    ) < 300
                ) {
                    message.error(intl.formatMessage({ id: 'draw.scrollScreenshot.limitTip' }));
                    return;
                }
            }

            let next = drawState;
            if (prev === drawState && prev !== DrawState.Idle) {
                if (drawState === DrawState.ScrollScreenshot) {
                    next = DrawState.Idle;
                } else {
                    next = DrawState.Select;
                }
            }

            if (next !== DrawState.Idle) {
                setCaptureStep(CaptureStep.Draw);
            } else {
                setCaptureStep(CaptureStep.Select);
            }

            switch (next) {
                case DrawState.Idle:
                    drawCacheLayerActionRef.current?.setEnable(false);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'hand',
                    });
                    break;
                case DrawState.Select:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'selection',
                    });
                    break;
                case DrawState.Rect:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'rectangle',
                        locked: true,
                    });
                    break;
                case DrawState.Diamond:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'diamond',
                        locked: true,
                    });
                    break;
                case DrawState.Ellipse:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'ellipse',
                        locked: true,
                    });
                    break;
                case DrawState.Arrow:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'arrow',
                        locked: true,
                    });
                    break;
                case DrawState.Line:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'line',
                        locked: true,
                    });
                    break;
                case DrawState.Pen:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'freedraw',
                        locked: true,
                    });
                    break;
                case DrawState.Text:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'text',
                        locked: true,
                    });
                    break;
                case DrawState.Blur:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'blur',
                        locked: true,
                    });
                    break;
                case DrawState.Eraser:
                    drawCacheLayerActionRef.current?.setEnable(true);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'eraser',
                        locked: true,
                    });
                    break;
                case DrawState.OcrDetect:
                    drawCacheLayerActionRef.current?.setEnable(false);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'hand',
                    });
                    onOcrDetect();
                    break;
                case DrawState.ExtraTools:
                    drawCacheLayerActionRef.current?.setEnable(false);
                    drawCacheLayerActionRef.current?.setActiveTool({
                        type: 'hand',
                    });
                    break;
                default:
                    break;
            }

            if (next === DrawState.ScrollScreenshot) {
                setEnableScrollScreenshot(true);
            } else {
                setEnableScrollScreenshot(false);
            }

            setDrawState(next);
        },
        [
            drawCacheLayerActionRef,
            getDrawState,
            intl,
            message,
            onOcrDetect,
            selectLayerActionRef,
            setCaptureStep,
            setDrawState,
        ],
    );

    const drawToolbarContextValue = useMemo(() => {
        return {
            drawToolbarRef,
            draggingRef,
            setDragging,
        };
    }, [drawToolbarRef, draggingRef, setDragging]);

    const canHandleScreenshotTypeRef = useRef(false);
    useStateSubscriber(CaptureEventPublisher, (event) => {
        if (event?.event === CaptureEvent.onCaptureReady) {
            canHandleScreenshotTypeRef.current = true;
        }
    });

    const onEnableChange = useCallback(
        (enable: boolean) => {
            enableRef.current = enable;
            dragButtonActionRef.current?.setEnable(enable);
            if (canHandleScreenshotTypeRef.current) {
                switch (getScreenshotType()) {
                    case ScreenshotType.Fixed:
                        onFixed();
                        break;
                    case ScreenshotType.OcrDetect:
                        onToolClick(DrawState.OcrDetect);
                        break;
                    case ScreenshotType.TopWindow:
                        onTopWindow();
                        break;
                    case ScreenshotType.Default:
                    default:
                        onToolClick(DrawState.Idle);
                        break;
                }
                canHandleScreenshotTypeRef.current = false;
            }
        },
        [onFixed, onToolClick, onTopWindow, getScreenshotType],
    );

    const setEnable = useCallback(
        (enable: boolean) => {
            if (enableRef.current === enable) {
                return;
            }

            onEnableChange(enable);
            updateEnableKeyEvent();
        },
        [onEnableChange, updateEnableKeyEvent],
    );

    useImperativeHandle(actionRef, () => {
        return {
            setEnable,
        };
    }, [setEnable]);

    const disableNormalScreenshotTool = enableScrollScreenshot;
    return (
        <div
            className="draw-toolbar-container"
            onMouseDown={(e) => {
                e.stopPropagation();
                e.preventDefault();
            }}
            ref={drawToolarContainerRef}
        >
            <DrawToolbarContext.Provider value={drawToolbarContextValue}>
                <div
                    onMouseEnter={() => {
                        setDrawToolbarState({ ...getDrawToolbarState(), mouseHover: true });
                    }}
                    onMouseLeave={() => {
                        setDrawToolbarState({ ...getDrawToolbarState(), mouseHover: false });
                    }}
                    className="draw-toolbar"
                    onDoubleClick={(e) => {
                        e.stopPropagation();
                        e.preventDefault();
                    }}
                    ref={drawToolbarRef}
                >
                    <Flex align="center" gap={token.paddingXS}>
                        <DragButton actionRef={dragButtonActionRef} />

                        {/* 默认状态 */}
                        <ToolButton
                            componentKey={KeyEventKey.MoveTool}
                            icon={<DragOutlined />}
                            drawState={DrawState.Idle}
                            onClick={() => {
                                onToolClick(DrawState.Idle);
                            }}
                        />

                        {/* 选择状态 */}
                        <ToolButton
                            componentKey={KeyEventKey.SelectTool}
                            icon={<ArrowSelectIcon style={{ fontSize: '1.08em' }} />}
                            drawState={DrawState.Select}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Select);
                            }}
                        />

                        <div className="draw-toolbar-splitter" />

                        {/* 矩形 */}
                        <ToolButton
                            componentKey={KeyEventKey.RectTool}
                            icon={<RectIcon style={{ fontSize: '1em' }} />}
                            disable={disableNormalScreenshotTool}
                            drawState={DrawState.Rect}
                            onClick={() => {
                                onToolClick(DrawState.Rect);
                            }}
                        />

                        {/* 菱形 */}
                        <ToolButton
                            componentKey={KeyEventKey.DiamondTool}
                            icon={<DiamondIcon style={{ fontSize: '1em' }} />}
                            drawState={DrawState.Diamond}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Diamond);
                            }}
                        />

                        {/* 椭圆 */}
                        <ToolButton
                            componentKey={KeyEventKey.EllipseTool}
                            icon={<CircleIcon style={{ fontSize: '1em' }} />}
                            drawState={DrawState.Ellipse}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Ellipse);
                            }}
                        />

                        {/* 箭头 */}
                        <ToolButton
                            componentKey={KeyEventKey.ArrowTool}
                            icon={<ArrowIcon style={{ fontSize: '0.83em' }} />}
                            drawState={DrawState.Arrow}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Arrow);
                            }}
                        />

                        {/* 线条 */}
                        <ToolButton
                            componentKey={KeyEventKey.LineTool}
                            icon={<LineIcon style={{ fontSize: '1.16em' }} />}
                            drawState={DrawState.Line}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Line);
                            }}
                        />

                        {/* 画笔 */}
                        <ToolButton
                            componentKey={KeyEventKey.PenTool}
                            icon={<PenIcon style={{ fontSize: '1.08em' }} />}
                            drawState={DrawState.Pen}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Pen);
                            }}
                        />

                        {/* 文本 */}
                        <ToolButton
                            componentKey={KeyEventKey.TextTool}
                            icon={<TextIcon style={{ fontSize: '1.08em' }} />}
                            drawState={DrawState.Text}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Text);
                            }}
                        />

                        {/* 模糊 */}
                        <ToolButton
                            componentKey={KeyEventKey.BlurTool}
                            icon={<MosaicIcon />}
                            drawState={DrawState.Blur}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Blur);
                            }}
                        />

                        {/* 橡皮擦 */}
                        <ToolButton
                            componentKey={KeyEventKey.EraserTool}
                            icon={<EraserIcon style={{ fontSize: '0.9em' }} />}
                            drawState={DrawState.Eraser}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.Eraser);
                            }}
                        />

                        <div className="draw-toolbar-splitter" />

                        <HistoryControls disable={enableScrollScreenshot} />

                        <div className="draw-toolbar-splitter" />

                        {/* 额外工具 */}
                        <ToolButton
                            componentKey={KeyEventKey.ExtraToolsTool}
                            icon={<AppstoreOutlined />}
                            drawState={DrawState.ExtraTools}
                            extraDrawState={[DrawState.ScanQrcode]}
                            disable={enableScrollScreenshot}
                            onClick={() => {
                                onToolClick(DrawState.ExtraTools);
                            }}
                        />

                        {/* 固定到屏幕 */}
                        <ToolButton
                            componentKey={KeyEventKey.FixedTool}
                            icon={<FixedIcon style={{ fontSize: '1.1em' }} />}
                            drawState={DrawState.Fixed}
                            disable={enableScrollScreenshot}
                            onClick={() => {
                                onFixed();
                            }}
                        />

                        {/* OCR */}
                        <ToolButton
                            componentKey={KeyEventKey.OcrDetectTool}
                            icon={<OcrDetectIcon style={{ fontSize: '0.88em' }} />}
                            drawState={DrawState.OcrDetect}
                            disable={disableNormalScreenshotTool}
                            onClick={() => {
                                onToolClick(DrawState.OcrDetect);
                            }}
                        />

                        {/* 滚动截图 */}
                        <ToolButton
                            componentKey={KeyEventKey.ScrollScreenshotTool}
                            icon={
                                <div style={{ position: 'relative', top: '0.11em' }}>
                                    <ScrollScreenshotIcon style={{ fontSize: '1.2em' }} />
                                </div>
                            }
                            drawState={DrawState.ScrollScreenshot}
                            onClick={() => {
                                onToolClick(DrawState.ScrollScreenshot);
                            }}
                        />

                        {/* 保存截图 */}
                        <ToolButton
                            componentKey={KeyEventKey.SaveTool}
                            icon={<SaveIcon style={{ fontSize: '1em' }} />}
                            drawState={DrawState.Save}
                            onClick={() => {
                                onSave();
                            }}
                        />

                        <div className="draw-toolbar-splitter" />

                        {/* 取消截图 */}
                        <ToolButton
                            componentKey={KeyEventKey.CancelTool}
                            icon={
                                <CloseOutlined
                                    style={{ fontSize: '0.83em', color: token.colorError }}
                                />
                            }
                            confirmTip={<FormattedMessage id="draw.cancel.tip1" />}
                            drawState={DrawState.Cancel}
                            onClick={() => {
                                onCancel();
                            }}
                        />

                        {/* 复制截图 */}
                        <ToolButton
                            componentKey={KeyEventKey.CopyTool}
                            icon={
                                <CopyOutlined
                                    style={{ fontSize: '0.92em', color: token.colorPrimary }}
                                />
                            }
                            drawState={DrawState.Copy}
                            onClick={() => {
                                onCopyToClipboard();
                            }}
                        />
                    </Flex>
                </div>

                <BlurTool />
                <ScrollScreenshot />
            </DrawToolbarContext.Provider>
            <style jsx>{`
                .draw-toolbar-container {
                    pointer-events: none;
                    user-select: none;
                    position: absolute;
                    z-index: ${zIndexs.Draw_Toolbar};
                    top: 0;
                    left: 0;
                    transition: opacity ${token.motionDurationFast} ${token.motionEaseInOut};
                }

                .draw-toolbar {
                    position: absolute;
                    opacity: 0;
                }

                .draw-toolbar {
                    padding: ${token.paddingXXS}px ${token.paddingSM}px;
                    box-sizing: border-box;
                    background-color: ${token.colorBgContainer};
                    border-radius: ${token.borderRadiusLG}px;
                    cursor: default; /* 防止非拖动区域也变成可拖动状态 */
                    color: ${token.colorText};
                    box-shadow: 0 0 3px 0px ${token.colorPrimaryHover};
                    transition: opacity ${token.motionDurationFast} ${token.motionEaseInOut};
                }

                .draw-subtoolbar {
                    opacity: 0;
                }

                .draw-subtoolbar-container {
                    position: absolute;
                    right: 0;
                    bottom: calc(-100% - ${token.marginXXS}px);
                    height: 100%;
                }

                :global(.drag-button) {
                    color: ${token.colorTextQuaternary};
                    cursor: move;
                }

                .draw-toolbar :global(.draw-toolbar-drag) {
                    font-size: 18px;
                    margin-right: -3px;
                    margin-left: -3px;
                }

                .draw-toolbar-container :global(.ant-btn) :global(.ant-btn-icon) {
                    font-size: 24px;
                }

                .draw-toolbar-container :global(.ant-btn-icon) {
                    display: flex;
                    align-items: center;
                }

                .draw-toolbar-container :global(.draw-toolbar-splitter),
                .draw-toolbar-splitter {
                    width: 1px;
                    height: 0.83em;
                    background-color: ${token.colorBorder};
                    margin: 0 ${token.marginXXS}px;
                }
            `}</style>
        </div>
    );
};

export const DrawToolbar = React.memo(DrawToolbarCore);
