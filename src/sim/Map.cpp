#include "Map.h"

#include <QFile>
#include <QPair>

#include "Directory.h"
#include "FontImage.h"
#include "Layout.h"
#include "Logging.h"
#include "Param.h"
#include "SimUtilities.h"
#include "Screen.h"
#include "State.h"
#include "TransformationMatrix.h"
#include "units/Seconds.h"

namespace mms {

Map::Map(const Model* model, Lens* lens, QWidget* parent) :
        QOpenGLWidget(parent),
        m_model(model),
        m_lens(lens) {

    // Continuously refresh the widget
	connect(
        this, &Map::frameSwapped,
        this, static_cast<void (Map::*)()>(&Map::update)
    );
}

QVector<QString> Map::getOpenGLVersionInfo() {
    static QVector<QString> openGLVersionInfo;
    if (openGLVersionInfo.empty()) {
        QString glType = context()->isOpenGLES() ? "OpenGL ES" : "OpenGL";
        QString glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        QString glProfile;
        switch (format().profile()) {
            case QSurfaceFormat::NoProfile:
                glProfile = "NoProfile";
                break;
            case QSurfaceFormat::CoreProfile:
                glProfile = "CoreProfile";
                break;
            case QSurfaceFormat::CompatibilityProfile:
                glProfile = "CompatibilityProfile";
                break;
        }
        openGLVersionInfo = {glType, glVersion, glProfile};
    }
    return openGLVersionInfo;
}

void Map::initOpenGLLogger() {
    if (m_openGLLogger.initialize()) {
        m_openGLLogger.startLogging(QOpenGLDebugLogger::SynchronousLogging);
        m_openGLLogger.enableMessages();
        m_openGLLogger.disableMessages(
            QOpenGLDebugMessage::AnySource,
            QOpenGLDebugMessage::AnyType,
            QOpenGLDebugMessage::NotificationSeverity
        );
    }
}

void Map::initializeGL() {

    // Contains all initialization that requires an OpenGL context

    // First, initialize the logger
    initOpenGLLogger();

    // Make it possible to call gl functions directly
    initializeOpenGLFunctions();

    // Set some gl values
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, S()->wireframeMode() ? GL_LINE : GL_FILL);

    // Initialize the polygon and texture programs
    initPolygonProgram();
    initTextureProgram();
}

void Map::paintGL() {

    // Determine the starting index of the mouse
    static const int mouseTrianglesStartingIndex = m_lens->getGraphicCpuBuffer()->size();

    // Get the current mouse translation and rotation
    Cartesian currentMouseTranslation = m_model->getMouse()->getCurrentTranslation();
    Radians currentMouseRotation = m_model->getMouse()->getCurrentRotation();

    // Make space for mouse updates and fill the CPU buffer with new mouse triangles
    m_lens->getGraphicCpuBuffer()->erase(
        m_lens->getGraphicCpuBuffer()->begin() + mouseTrianglesStartingIndex,
        m_lens->getGraphicCpuBuffer()->end());
    m_lens->getMouseGraphic()->draw(currentMouseTranslation, currentMouseRotation);

    // Re-populate both vertex buffer objects
    repopulateVertexBufferObjects();

    // Clear the screen
    glClear(GL_COLOR_BUFFER_BIT);

    // Enable scissoring so that the maps are only draw in specified locations.
    glEnable(GL_SCISSOR_TEST);

    // Determine the type of map to draw
    LayoutType layoutType = S()->layoutType();

    // Draw the tiles
    drawMap(
        layoutType,
        currentMouseTranslation,
        currentMouseRotation,
        &m_polygonProgram,
        &m_polygonVAO,
        0,
        3 * mouseTrianglesStartingIndex
    );

    // Overlay the tile text
    drawMap(
        layoutType,
        currentMouseTranslation,
        currentMouseRotation,
        &m_textureProgram,
        &m_textureVAO,
        0,
        3 * m_lens->getTextureCpuBuffer()->size()
    );

    // Draw the mouse
    drawMap(
        layoutType,
        currentMouseTranslation,
        currentMouseRotation,
        &m_polygonProgram,
        &m_polygonVAO,
        3 * mouseTrianglesStartingIndex,
        3 * (m_lens->getGraphicCpuBuffer()->size() - mouseTrianglesStartingIndex)
    );

    // Disable scissoring so that the glClear can take effect, and so that
    // drawn text isn't clipped at all
    glDisable(GL_SCISSOR_TEST);
}

void Map::resizeGL(int width, int height) {
    m_windowWidth = width;
    m_windowHeight = height;
}

void Map::initPolygonProgram() {

	m_polygonProgram.addShaderFromSourceCode(
		QOpenGLShader::Vertex,
		R"(
            uniform mat4 transformationMatrix;
            attribute vec2 coordinate;
            attribute vec4 inColor;
            varying vec4 outColor;
            void main(void) {
                gl_Position = transformationMatrix * vec4(coordinate, 0.0, 1.0);
                outColor = inColor;
            }
		)"
	);
	m_polygonProgram.addShaderFromSourceCode(
		QOpenGLShader::Fragment,
		R"(
            varying vec4 outColor;
			void main(void) {
			   gl_FragColor = outColor;
			}
		)"
	);
	m_polygonProgram.link();
	m_polygonProgram.bind();

	m_polygonVAO.create();
	m_polygonVAO.bind();

	m_polygonVBO.create();
	m_polygonVBO.bind();
	m_polygonVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);

	m_polygonProgram.enableAttributeArray("coordinate");
	m_polygonProgram.setAttributeBuffer(
        "coordinate", // name
        GL_DOUBLE, // type
        0, // offset (bytes)
        2, // tupleSize (number of elements in the attribute array)
        6 * sizeof(double) // stride (bytes between vertices)
    );

	m_polygonProgram.enableAttributeArray("inColor");
	m_polygonProgram.setAttributeBuffer(
        "inColor", // name
        GL_DOUBLE, // type
        2 * sizeof(double), // offset (bytes)
        4, // tupleSize (number of elements in the attribute array)
        6 * sizeof(double) // stride (bytes between vertices)
    );

	m_polygonVBO.release();
	m_polygonVAO.release();
	m_polygonProgram.release();
}

void Map::initTextureProgram() {

	m_textureProgram.addShaderFromSourceCode(
		QOpenGLShader::Vertex,
		R"(
            uniform mat4 transformationMatrix;
            attribute vec2 coordinate;
            attribute vec2 inTextureCoordinate;
            varying vec2 outTextureCoordinate;
            void main() {
                gl_Position = transformationMatrix * vec4(coordinate, 0.0, 1.0);
                outTextureCoordinate = inTextureCoordinate;
            }
		)"
	);
	m_textureProgram.addShaderFromSourceCode(
		QOpenGLShader::Fragment,
		R"(
            uniform sampler2D texture;
            varying vec2 outTextureCoordinate;
            void main() {
                gl_FragColor = texture2D(texture, outTextureCoordinate);
            }
		)"
	);
	m_textureProgram.link();
	m_textureProgram.bind();

	m_textureVAO.create();
	m_textureVAO.bind();

	m_textureVBO.create();
	m_textureVBO.bind();
	m_textureVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);

	m_textureProgram.enableAttributeArray("coordinate");
	m_textureProgram.setAttributeBuffer(
        "coordinate", // name
        GL_DOUBLE, // type
        0, // offset (bytes)
        2, // tupleSize (number of elements in the attribute array)
        4 * sizeof(double) // stride (bytes between vertices)
    );

	m_textureProgram.enableAttributeArray("inTextureCoordinate");
	m_textureProgram.setAttributeBuffer(
        "inTextureCoordinate", // name
        GL_DOUBLE, // type
        2 * sizeof(double), // offset (bytes)
        2, // tupleSize (number of elements in the attribute array)
        4 * sizeof(double) // stride (bytes between vertices)
    );

    // Load the bitmap texture into the texture atlas
    m_textureAtlas = new QOpenGLTexture(
        QImage(FontImage::get()->imageFilePath()).mirrored());

	m_polygonVBO.release();
	m_polygonVAO.release();
	m_polygonProgram.release();
}

void Map::repopulateVertexBufferObjects() {

    // TODO: MACK - we shouldn't need literal TriangleGraphic and
    // TriangleTexture here - these should be method calls on the lens object

    // Overwrite the polygon vertex buffer object data
    m_polygonVBO.bind();
	m_polygonVBO.allocate(
        &(m_lens->getGraphicCpuBuffer()->front()),
        m_lens->getGraphicCpuBuffer()->size() * sizeof(TriangleGraphic)
    );
    m_polygonVBO.release();

    // Overwrite the texture vertex buffer object data
    m_textureVBO.bind();
	m_textureVBO.allocate(
        &(m_lens->getTextureCpuBuffer()->front()),
        m_lens->getTextureCpuBuffer()->size() * sizeof(TriangleTexture)
    );
    m_textureVBO.release();
}

void Map::drawMap(
        LayoutType type,
        const Coordinate& currentMouseTranslation,
        const Angle& currentMouseRotation,
        QOpenGLShaderProgram* program,
        QOpenGLVertexArrayObject* vao,
        int vboStartingIndex,
        int count) {

    // Get the physical size of the maze (in meters)
    double physicalMazeWidth = P()->wallWidth() + m_model->getMaze()->getWidth() * (P()->wallWidth() + P()->wallLength());
    double physicalMazeHeight = P()->wallWidth() + m_model->getMaze()->getHeight() * (P()->wallWidth() + P()->wallLength());
    // TODO: MACK - these should be distances, not doubles
    QPair<double, double> physicalMazeSize = {physicalMazeWidth, physicalMazeHeight};

    // Start using the program and vertex array object
    program->bind();
    vao->bind();

    // If it's the texture program, bind the texture and set the uniform
    if (program == &m_textureProgram) {
        glActiveTexture(GL_TEXTURE0);
        m_textureAtlas->bind();
        program->setUniformValue("texture", 0);
    }
    
    // Render the full map
    if (type == LayoutType::FULL) {

        QPair<int, int> fullMapPosition = Layout::getFullMapPosition();
        QPair<int, int> fullMapSize = Layout::getFullMapSize(m_windowWidth, m_windowHeight);

        // TODO: MACK
        auto matrix = TransformationMatrix::getFullMapTransformationMatrix(
            Meters(P()->wallWidth()),
            physicalMazeSize,
            fullMapPosition,
            fullMapSize,
            {m_windowWidth, m_windowHeight}
        );
        QMatrix4x4 transformationMatrix(
            matrix.at(0), matrix.at(1), matrix.at(2), matrix.at(3),
            matrix.at(4), matrix.at(5), matrix.at(6), matrix.at(7),
            matrix.at(8), matrix.at(9), matrix.at(10), matrix.at(11),
            matrix.at(12), matrix.at(13), matrix.at(14), matrix.at(15)
        );

        glScissor(fullMapPosition.first, fullMapPosition.second, fullMapSize.first, fullMapSize.second);
        program->setUniformValue("transformationMatrix", transformationMatrix);
        glDrawArrays(GL_TRIANGLES, vboStartingIndex, count);

    }

    // Render the zoomed map
    else {

        QPair<int, int> zoomedMapPosition = Layout::getZoomedMapPosition();
        QPair<int, int> zoomedMapSize = Layout::getZoomedMapSize(m_windowWidth, m_windowHeight);

        // TODO: MACK
        auto matrix2 = TransformationMatrix::getZoomedMapTransformationMatrix(
            physicalMazeSize,
            zoomedMapPosition,
            zoomedMapSize,
            {m_windowWidth, m_windowHeight},
            Screen::get()->pixelsPerMeter(),
            S()->zoomedMapScale(),
            S()->rotateZoomedMap(),
            m_model->getMouse()->getInitialTranslation(),
            currentMouseTranslation,
            currentMouseRotation
        );
        QMatrix4x4 transformationMatrix2(
            matrix2.at(0), matrix2.at(1), matrix2.at(2), matrix2.at(3),
            matrix2.at(4), matrix2.at(5), matrix2.at(6), matrix2.at(7),
            matrix2.at(8), matrix2.at(9), matrix2.at(10), matrix2.at(11),
            matrix2.at(12), matrix2.at(13), matrix2.at(14), matrix2.at(15)
        );

        glScissor(zoomedMapPosition.first, zoomedMapPosition.second, zoomedMapSize.first, zoomedMapSize.second);
        program->setUniformValue("transformationMatrix", transformationMatrix2);
        glDrawArrays(GL_TRIANGLES, vboStartingIndex, count);
    }

    // If it's the texture program, we should additionally unbind the texture
    if (program == &m_textureProgram) {
        m_textureAtlas->release();
    }

    // Stop using the program and vertex array object
    vao->release();
}

} // namespace mms
