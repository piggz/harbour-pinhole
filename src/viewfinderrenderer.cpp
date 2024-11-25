#include "viewfinderrenderer.h"

#include <array>

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QStringList>

#include <libcamera/formats.h>

#include "image.h"

ViewFinderRenderer::ViewFinderRenderer()
    : buffer_(nullptr),
      colorSpace_(libcamera::ColorSpace::Raw), image_(nullptr),
      vertexBuffer_(QOpenGLBuffer::VertexBuffer)
{
    qDebug() << Q_FUNC_INFO;
}

ViewFinderRenderer::~ViewFinderRenderer()
{
    removeShader();
}

void ViewFinderRenderer::init()
{
    static bool _init = false;
    initializeOpenGLFunctions();

    qDebug() << Q_FUNC_INFO;
    if (!_init) {
        glEnable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);

        static const GLfloat coordinates[2][4][2]{
            {
                //Vertex coordinates
                { -1.0f, -1.0f },
                { -1.0f, +1.0f },
                { +1.0f, +1.0f },
                { +1.0f, -1.0f },
            },
            {
                // Texture coordinates
                { 0.0f, 1.0f },
                { 0.0f, 0.0f },
                { 1.0f, 0.0f },
                { 1.0f, 1.0f },
            },
        };

        vertexBuffer_.create();
        vertexBuffer_.bind();
        vertexBuffer_.allocate(coordinates, sizeof(coordinates));

        _init = true;
    }

    /* Create Vertex Shader */
    if (!createVertexShader())
        qWarning() << "[ViewFinderGL]: create vertex shader failed.";
}

void ViewFinderRenderer::render()
{
    qDebug() << Q_FUNC_INFO;
    init();

    if (!fragmentShader_) {
        if (!createFragmentShader()) {
            qWarning() << "[ViewFinderRenderer]:"
                       << "create fragment shader failed.";
            return;
        }
    }

    if (image_) {
        glClearColor(0.5, 1.0, 0.5, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        doRender();
        //glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

}

static const QList<libcamera::PixelFormat> supportedFormats{
    /* YUV - packed (single plane) */
    libcamera::formats::UYVY,
            libcamera::formats::VYUY,
            libcamera::formats::YUYV,
            libcamera::formats::YVYU,
            /* YUV - semi planar (two planes) */
            libcamera::formats::NV12,
            libcamera::formats::NV21,
            libcamera::formats::NV16,
            libcamera::formats::NV61,
            libcamera::formats::NV24,
            libcamera::formats::NV42,
            /* YUV - fully planar (three planes) */
            libcamera::formats::YUV420,
            libcamera::formats::YVU420,
            /* RGB */
            libcamera::formats::ABGR8888,
            libcamera::formats::ARGB8888,
            libcamera::formats::BGRA8888,
            libcamera::formats::RGBA8888,
            libcamera::formats::BGR888,
            libcamera::formats::RGB888,
            /* Raw Bayer 8-bit */
            libcamera::formats::SBGGR8,
            libcamera::formats::SGBRG8,
            libcamera::formats::SGRBG8,
            libcamera::formats::SRGGB8,
            /* Raw Bayer 10-bit packed */
            libcamera::formats::SBGGR10_CSI2P,
            libcamera::formats::SGBRG10_CSI2P,
            libcamera::formats::SGRBG10_CSI2P,
            libcamera::formats::SRGGB10_CSI2P,
            /* Raw Bayer 12-bit packed */
            libcamera::formats::SBGGR12_CSI2P,
            libcamera::formats::SGBRG12_CSI2P,
            libcamera::formats::SGRBG12_CSI2P,
            libcamera::formats::SRGGB12_CSI2P,
};

const QList<libcamera::PixelFormat> &ViewFinderRenderer::nativeFormats() const
{
    return supportedFormats;
}

int ViewFinderRenderer::setFormat(const libcamera::PixelFormat &format, const QSize &size,
                                  const libcamera::ColorSpace &colorSpace,
                                  unsigned int stride)
{
    qDebug() << Q_FUNC_INFO;
    if (format != format_ || colorSpace != colorSpace_) {
        /*
         * If the fragment already exists, remove it and create a new
         * one for the new format.
         */
        if (shaderProgram_.isLinked()) {
            shaderProgram_.release();
            shaderProgram_.removeShader(fragmentShader_.get());
            fragmentShader_.reset();
        }

        if (!selectFormat(format))
            return -1;

        selectColorSpace(colorSpace);

        format_ = format;
        colorSpace_ = colorSpace;
    }

    size_ = size;
    stride_ = stride;

    return 0;
}

void ViewFinderRenderer::stop()
{
    if (buffer_) {
        buffer_ = nullptr;
        image_ = nullptr;
    }
}

void ViewFinderRenderer::renderImage(libcamera::FrameBuffer *buffer, Image *image, QList<QRectF>)
{
    qDebug() << Q_FUNC_INFO;
    //if (buffer_)
    //	renderComplete(buffer_);

    image_ = image;
    buffer_ = buffer;
    update();
}

bool ViewFinderRenderer::selectFormat(const libcamera::PixelFormat &format)
{
    qDebug() << Q_FUNC_INFO;
    bool ret = true;

    /* Set min/mag filters to GL_LINEAR by default. */
    textureMinMagFilters_ = GL_LINEAR;

    /* Use identity.vert as the default vertex shader. */
    vertexShaderFile_ = QStringLiteral(":qml/assets/identity.vert");

    fragmentShaderDefines_.clear();

    switch (format) {
    case libcamera::formats::NV12:
        horzSubSample_ = 2;
        vertSubSample_ = 2;
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_UV"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_2_planes.frag");
        break;
    case libcamera::formats::NV21:
        horzSubSample_ = 2;
        vertSubSample_ = 2;
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_VU"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_2_planes.frag");
        break;
    case libcamera::formats::NV16:
        horzSubSample_ = 2;
        vertSubSample_ = 1;
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_UV"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_2_planes.frag");
        break;
    case libcamera::formats::NV61:
        horzSubSample_ = 2;
        vertSubSample_ = 1;
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_VU"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_2_planes.frag");
        break;
    case libcamera::formats::NV24:
        horzSubSample_ = 1;
        vertSubSample_ = 1;
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_UV"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_2_planes.frag");
        break;
    case libcamera::formats::NV42:
        horzSubSample_ = 1;
        vertSubSample_ = 1;
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_VU"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_2_planes.frag");
        break;
    case libcamera::formats::YUV420:
        horzSubSample_ = 2;
        vertSubSample_ = 2;
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_3_planes.frag");
        break;
    case libcamera::formats::YVU420:
        horzSubSample_ = 2;
        vertSubSample_ = 2;
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_3_planes.frag");
        break;
    case libcamera::formats::UYVY:
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_UYVY"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_packed.frag");
        break;
    case libcamera::formats::VYUY:
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_VYUY"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_packed.frag");
        break;
    case libcamera::formats::YUYV:
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_YUYV"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_packed.frag");
        break;
    case libcamera::formats::YVYU:
        fragmentShaderDefines_.append(QStringLiteral("#define YUV_PATTERN_YVYU"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/YUV_packed.frag");
        break;
    case libcamera::formats::ABGR8888:
        fragmentShaderDefines_.append(QStringLiteral("#define RGB_PATTERN rgb"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/RGB.frag");
        break;
    case libcamera::formats::ARGB8888:
        fragmentShaderDefines_.append(QStringLiteral("#define RGB_PATTERN bgr"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/RGB.frag");
        break;
    case libcamera::formats::BGRA8888:
        fragmentShaderDefines_.append(QStringLiteral("#define RGB_PATTERN gba"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/RGB.frag");
        break;
    case libcamera::formats::RGBA8888:
        fragmentShaderDefines_.append(QStringLiteral("#define RGB_PATTERN abg"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/RGB.frag");
        break;
    case libcamera::formats::BGR888:
        fragmentShaderDefines_.append(QStringLiteral("#define RGB_PATTERN rgb"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/RGB.frag");
        break;
    case libcamera::formats::RGB888:
        fragmentShaderDefines_.append(QStringLiteral("#define RGB_PATTERN bgr"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/RGB.frag");
        break;
    case libcamera::formats::SBGGR8:
        firstRed_.setX(1.0);
        firstRed_.setY(1.0);
        vertexShaderFile_ = QStringLiteral(":qml/assets/bayer_8.vert");
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_8.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SGBRG8:
        firstRed_.setX(0.0);
        firstRed_.setY(1.0);
        vertexShaderFile_ = QStringLiteral(":qml/assets/bayer_8.vert");
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_8.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SGRBG8:
        firstRed_.setX(1.0);
        firstRed_.setY(0.0);
        vertexShaderFile_ = QStringLiteral(":qml/assets/bayer_8.vert");
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_8.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SRGGB8:
        firstRed_.setX(0.0);
        firstRed_.setY(0.0);
        vertexShaderFile_ = QStringLiteral(":qml/assets/bayer_8.vert");
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_8.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SBGGR10_CSI2P:
        firstRed_.setX(1.0);
        firstRed_.setY(1.0);
        fragmentShaderDefines_.append(QStringLiteral("#define RAW10P"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_1x_packed.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SGBRG10_CSI2P:
        firstRed_.setX(0.0);
        firstRed_.setY(1.0);
        fragmentShaderDefines_.append(QStringLiteral("#define RAW10P"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_1x_packed.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SGRBG10_CSI2P:
        firstRed_.setX(1.0);
        firstRed_.setY(0.0);
        fragmentShaderDefines_.append(QStringLiteral("#define RAW10P"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_1x_packed.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SRGGB10_CSI2P:
        firstRed_.setX(0.0);
        firstRed_.setY(0.0);
        fragmentShaderDefines_.append(QStringLiteral("#define RAW10P"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_1x_packed.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SBGGR12_CSI2P:
        firstRed_.setX(1.0);
        firstRed_.setY(1.0);
        fragmentShaderDefines_.append(QStringLiteral("#define RAW12P"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_1x_packed.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SGBRG12_CSI2P:
        firstRed_.setX(0.0);
        firstRed_.setY(1.0);
        fragmentShaderDefines_.append(QStringLiteral("#define RAW12P"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_1x_packed.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SGRBG12_CSI2P:
        firstRed_.setX(1.0);
        firstRed_.setY(0.0);
        fragmentShaderDefines_.append(QStringLiteral("#define RAW12P"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_1x_packed.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    case libcamera::formats::SRGGB12_CSI2P:
        firstRed_.setX(0.0);
        firstRed_.setY(0.0);
        fragmentShaderDefines_.append(QStringLiteral("#define RAW12P"));
        fragmentShaderFile_ = QStringLiteral(":qml/assets/bayer_1x_packed.frag");
        textureMinMagFilters_ = GL_NEAREST;
        break;
    default:
        ret = false;
        qWarning() << "[ViewFinderRenderer]:"
                   << "format not supported.";
        break;
    };

    qDebug() << Q_FUNC_INFO << vertexShaderFile_ << fragmentShaderFile_;

    m_hadConfig = true;
    return ret;
}

void ViewFinderRenderer::selectColorSpace(const libcamera::ColorSpace &colorSpace)
{
    std::array<double, 9> yuv2rgb;

    /* OpenGL stores arrays in column-major order. */
    switch (colorSpace.ycbcrEncoding) {
    case libcamera::ColorSpace::YcbcrEncoding::None:
    default:
        yuv2rgb = {
            1.0000,  0.0000,  0.0000,
            0.0000,  1.0000,  0.0000,
            0.0000,  0.0000,  1.0000,
        };
        break;

    case libcamera::ColorSpace::YcbcrEncoding::Rec601:
        yuv2rgb = {
            1.0000,  1.0000,  1.0000,
            0.0000, -0.3441,  1.7720,
            1.4020, -0.7141,  0.0000,
        };
        break;

    case libcamera::ColorSpace::YcbcrEncoding::Rec709:
        yuv2rgb = {
            1.0000,  1.0000,  1.0000,
            0.0000, -0.1873,  1.8856,
            1.5748, -0.4681,  0.0000,
        };
        break;

    case libcamera::ColorSpace::YcbcrEncoding::Rec2020:
        yuv2rgb = {
            1.0000,  1.0000,  1.0000,
            0.0000, -0.1646,  1.8814,
            1.4746, -0.5714,  0.0000,
        };
        break;
    }

    double offset;

    switch (colorSpace.range) {
    case libcamera::ColorSpace::Range::Full:
    default:
        offset = 0.0;
        break;

    case libcamera::ColorSpace::Range::Limited:
        offset = 16.0;

        for (unsigned int i = 0; i < 3; ++i)
            yuv2rgb[i] *= 255.0 / 219.0;
        for (unsigned int i = 4; i < 9; ++i)
            yuv2rgb[i] *= 255.0 / 224.0;
        break;
    }

    QStringList matrix;

    for (double coeff : yuv2rgb)
        matrix.append(QString::number(coeff, 'f'));

    fragmentShaderDefines_.append(QStringLiteral("#define YUV2RGB_MATRIX ") + matrix.join(QStringLiteral(", ")));
    fragmentShaderDefines_.append(QStringLiteral("#define YUV2RGB_Y_OFFSET %1")
                                  .arg(offset, 0, 'f', 1));
}

bool ViewFinderRenderer::createVertexShader()
{
    qDebug() << Q_FUNC_INFO << vertexShaderFile_;

    if (vertexShaderFile_.isEmpty()) {
        qDebug() << "vertef shader not yet set";
        return false;
    }
    /* Create Vertex Shader */
    vertexShader_ = std::make_unique<QOpenGLShader>(QOpenGLShader::Vertex);

    /* Compile the vertex shader */
    if (!vertexShader_->compileSourceFile(vertexShaderFile_)) {
        qWarning() << "[ViewFinderRenderer::createVertexShader]:" << vertexShader_->log();
        return false;
    }

    shaderProgram_.addShader(vertexShader_.get());
    return true;
}

bool ViewFinderRenderer::createFragmentShader()
{
    qDebug() << Q_FUNC_INFO << fragmentShaderFile_;
    int attributeVertex;
    int attributeTexture;

    if (fragmentShaderFile_.isEmpty()) {
        qDebug() << "fragemnt file not yet set";
        return false;
    }
    /*
     * Create the fragment shader, compile it, and add it to the shader
     * program. The #define macros stored in fragmentShaderDefines_, if
     * any, are prepended to the source code.
     */
    fragmentShader_ = std::make_unique<QOpenGLShader>(QOpenGLShader::Fragment);

    QFile file(fragmentShaderFile_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Shader 1" << fragmentShaderFile_ << "not found";
        return false;
    }

    QString defines = fragmentShaderDefines_.join(QStringLiteral("\n")) + QStringLiteral("\n");
    QByteArray src = file.readAll();
    src.prepend(defines.toUtf8());

    if (!fragmentShader_->compileSourceCode(src)) {
        qWarning() << "[ViewFinderRenderer]2:" << fragmentShader_->log();
        return false;
    }

    shaderProgram_.addShader(fragmentShader_.get());

    /* Link shader pipeline */
    if (!shaderProgram_.link()) {
        qWarning() << "[ViewFinderRenderer]3:" << shaderProgram_.log();
        //TODO close();
    }

    /* Bind shader pipeline for use */
    if (!shaderProgram_.bind()) {
        qWarning() << "[ViewFinderRenderer]4:" << shaderProgram_.log();
        //TODO close();
    }

    attributeVertex = shaderProgram_.attributeLocation("vertexIn");
    attributeTexture = shaderProgram_.attributeLocation("textureIn");

    shaderProgram_.enableAttributeArray(attributeVertex);
    shaderProgram_.setAttributeBuffer(attributeVertex,
                                      GL_FLOAT,
                                      0,
                                      2,
                                      2 * sizeof(GLfloat));

    shaderProgram_.enableAttributeArray(attributeTexture);
    shaderProgram_.setAttributeBuffer(attributeTexture,
                                      GL_FLOAT,
                                      8 * sizeof(GLfloat),
                                      2,
                                      2 * sizeof(GLfloat));

    textureUniformY_ = shaderProgram_.uniformLocation("tex_y");
    textureUniformU_ = shaderProgram_.uniformLocation("tex_u");
    textureUniformV_ = shaderProgram_.uniformLocation("tex_v");
    textureUniformStep_ = shaderProgram_.uniformLocation("tex_step");
    textureUniformSize_ = shaderProgram_.uniformLocation("tex_size");
    textureUniformStrideFactor_ = shaderProgram_.uniformLocation("stride_factor");
    textureUniformBayerFirstRed_ = shaderProgram_.uniformLocation("tex_bayer_first_red");

    /* Create the textures. */
    for (std::unique_ptr<QOpenGLTexture> &texture : textures_) {
        if (texture)
            continue;

        texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        texture->create();
    }

    qDebug() << "Created fragement shader";
    return true;
}

void ViewFinderRenderer::configureTexture(QOpenGLTexture &texture)
{
    glBindTexture(GL_TEXTURE_2D, texture.textureId());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    textureMinMagFilters_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    textureMinMagFilters_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void ViewFinderRenderer::removeShader()
{
    if (shaderProgram_.isLinked()) {
        shaderProgram_.release();
        shaderProgram_.removeAllShaders();
    }
}

void ViewFinderRenderer::doRender()
{
    qDebug() << Q_FUNC_INFO << format_;
    /* Stride of the first plane, in pixels. */
    unsigned int stridePixels;

    switch (format_) {
    case libcamera::formats::NV12:
    case libcamera::formats::NV21:
    case libcamera::formats::NV16:
    case libcamera::formats::NV61:
    case libcamera::formats::NV24:
    case libcamera::formats::NV42:
        /* Activate texture Y */
        glActiveTexture(GL_TEXTURE0);
        configureTexture(*textures_[0]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     stride_,
                     size_.height(),
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     image_->data(0).data());
        shaderProgram_.setUniformValue(textureUniformY_, 0);

        /* Activate texture UV/VU */
        glActiveTexture(GL_TEXTURE1);
        configureTexture(*textures_[1]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE_ALPHA,
                     stride_ / horzSubSample_,
                     size_.height() / vertSubSample_,
                     0,
                     GL_LUMINANCE_ALPHA,
                     GL_UNSIGNED_BYTE,
                     image_->data(1).data());
        shaderProgram_.setUniformValue(textureUniformU_, 1);

        stridePixels = stride_;
        break;

    case libcamera::formats::YUV420:
        /* Activate texture Y */
        glActiveTexture(GL_TEXTURE0);
        configureTexture(*textures_[0]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     stride_,
                     size_.height(),
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     image_->data(0).data());
        shaderProgram_.setUniformValue(textureUniformY_, 0);

        /* Activate texture U */
        glActiveTexture(GL_TEXTURE1);
        configureTexture(*textures_[1]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     stride_ / horzSubSample_,
                     size_.height() / vertSubSample_,
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     image_->data(1).data());
        shaderProgram_.setUniformValue(textureUniformU_, 1);

        /* Activate texture V */
        glActiveTexture(GL_TEXTURE2);
        configureTexture(*textures_[2]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     stride_ / horzSubSample_,
                     size_.height() / vertSubSample_,
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     image_->data(2).data());
        shaderProgram_.setUniformValue(textureUniformV_, 2);

        stridePixels = stride_;
        break;

    case libcamera::formats::YVU420:
        /* Activate texture Y */
        glActiveTexture(GL_TEXTURE0);
        configureTexture(*textures_[0]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     stride_,
                     size_.height(),
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     image_->data(0).data());
        shaderProgram_.setUniformValue(textureUniformY_, 0);

        /* Activate texture V */
        glActiveTexture(GL_TEXTURE2);
        configureTexture(*textures_[2]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     stride_ / horzSubSample_,
                     size_.height() / vertSubSample_,
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     image_->data(1).data());
        shaderProgram_.setUniformValue(textureUniformV_, 2);

        /* Activate texture U */
        glActiveTexture(GL_TEXTURE1);
        configureTexture(*textures_[1]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     stride_ / horzSubSample_,
                     size_.height() / vertSubSample_,
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     image_->data(2).data());
        shaderProgram_.setUniformValue(textureUniformU_, 1);

        stridePixels = stride_;
        break;

    case libcamera::formats::UYVY:
    case libcamera::formats::VYUY:
    case libcamera::formats::YUYV:
    case libcamera::formats::YVYU:
        /*
         * Packed YUV formats are stored in a RGBA texture to match the
         * OpenGL texel size with the 4 bytes repeating pattern in YUV.
         * The texture width is thus half of the image_ with.
         */
        glActiveTexture(GL_TEXTURE0);
        configureTexture(*textures_[0]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     stride_ / 4,
                     size_.height(),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     image_->data(0).data());
        shaderProgram_.setUniformValue(textureUniformY_, 0);

        /*
         * The shader needs the step between two texture pixels in the
         * horizontal direction, expressed in texture coordinate units
         * ([0, 1]). There are exactly width - 1 steps between the
         * leftmost and rightmost texels.
         */
        shaderProgram_.setUniformValue(textureUniformStep_,
                                       1.0f / (size_.width() / 2 - 1),
                                       1.0f /* not used */);

        stridePixels = stride_ / 2;
        break;

    case libcamera::formats::ABGR8888:
    case libcamera::formats::ARGB8888:
    case libcamera::formats::BGRA8888:
    case libcamera::formats::RGBA8888:
        glActiveTexture(GL_TEXTURE0);
        configureTexture(*textures_[0]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     stride_ / 4,
                     size_.height(),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     image_->data(0).data());
        shaderProgram_.setUniformValue(textureUniformY_, 0);

        stridePixels = stride_ / 4;
        break;

    case libcamera::formats::BGR888:
    case libcamera::formats::RGB888:
        glActiveTexture(GL_TEXTURE0);
        configureTexture(*textures_[0]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGB,
                     stride_ / 3,
                     size_.height(),
                     0,
                     GL_RGB,
                     GL_UNSIGNED_BYTE,
                     image_->data(0).data());
        shaderProgram_.setUniformValue(textureUniformY_, 0);

        stridePixels = stride_ / 3;
        break;

    case libcamera::formats::SBGGR8:
    case libcamera::formats::SGBRG8:
    case libcamera::formats::SGRBG8:
    case libcamera::formats::SRGGB8:
    case libcamera::formats::SBGGR10_CSI2P:
    case libcamera::formats::SGBRG10_CSI2P:
    case libcamera::formats::SGRBG10_CSI2P:
    case libcamera::formats::SRGGB10_CSI2P:
    case libcamera::formats::SBGGR12_CSI2P:
    case libcamera::formats::SGBRG12_CSI2P:
    case libcamera::formats::SGRBG12_CSI2P:
    case libcamera::formats::SRGGB12_CSI2P:
        /*
         * Raw Bayer 8-bit, and packed raw Bayer 10-bit/12-bit formats
         * are stored in a GL_LUMINANCE texture. The texture width is
         * equal to the stride.
         */
        glActiveTexture(GL_TEXTURE0);
        configureTexture(*textures_[0]);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     stride_,
                     size_.height(),
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     image_->data(0).data());
        shaderProgram_.setUniformValue(textureUniformY_, 0);
        shaderProgram_.setUniformValue(textureUniformBayerFirstRed_,
                                       firstRed_);
        shaderProgram_.setUniformValue(textureUniformSize_,
                                       size_.width(), /* in pixels */
                                       size_.height());
        shaderProgram_.setUniformValue(textureUniformStep_,
                                       1.0f / (stride_ - 1),
                                       1.0f / (size_.height() - 1));

        /*
         * The stride is already taken into account in the shaders, set
         * the generic stride factor to 1.0.
         */
        stridePixels = size_.width();
        break;

    default:
        stridePixels = size_.width();
        break;
    };

    /*
     * Compute the stride factor for the vertex shader, to map the
     * horizontal texture coordinate range [0.0, 1.0] to the active portion
     * of the image.
     */
    shaderProgram_.setUniformValue(textureUniformStrideFactor_,
                                   static_cast<float>(size_.width() - 1) /
                                   (stridePixels - 1));

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}
