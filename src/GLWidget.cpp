#include "GLWidget.h"

#include "MainWindow.h"

void GLWidget::initializeGL()
{
	// obtain a gl functions object and resolve all entry points
	// in this case we use QOpenGLFunctions_3_3_Core* gl functions
	// this supports glTexImage3D
	glf = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_3_Core>();
	if (!glf) { qWarning("Could not obtain OpenGL versions object"); exit(1); }
	glf->initializeOpenGLFunctions();

	// print glError messages
	logger = new QOpenGLDebugLogger(this);
	logger->initialize();
	connect(logger, &QOpenGLDebugLogger::messageLogged, this, &GLWidget::printDebugMsg);
	logger->startLogging();

	// load, compile and link vertex and fragment shaders
	initShaders();

	// initialize vertex buffer object for raw data of the cube defining the volume bounding box
	// the cube vertex positions are interpolated as colors in fragment shader
	// to yield all possible ray volume exit positions to be stored in a texture for ray traversal.
	initVolumeBBoxCubeVBO();

	// load 1D transfer function texture from image
	loadTransferFunction1DTex("../transferfunctions/tff_flame.png");

	// init framebuffer to hold a 2D texture for volume exit positions of orthogonal rays
	// texture is autogenerated on framebuffer creation and will be filled with data later.
	initRayVolumeExitPosMapFramebuffer();

	viewOffset = QVector3D(0.f, 0.f, 1.8f);

}

void GLWidget::initShaders()
{
	raycastShader = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
	raycastShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "../src/shaders/raycast_shader.vert");
	raycastShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "../src/shaders/raycast_shader.frag");
	raycastShader->link();

	rayVolumeExitPosMapShader = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
	rayVolumeExitPosMapShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "../src/shaders/rayvolumeexitposmap_shader.vert");
	rayVolumeExitPosMapShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "../src/shaders/rayvolumeexitposmap_shader.frag");
	rayVolumeExitPosMapShader->link();

}

void GLWidget::initVolumeBBoxCubeVBO()
{
	// generate vertex array object (vao).
	// subsequent bindings related to vertex buffer data and shader variables
	// are stored by the bound vao, so they can conveniently be reused later.
	volumeBBoxCubeVAO.create();
	volumeBBoxCubeVAO.bind();

	// generate vertex buffer object (vbo) for raw vertex data
	QOpenGLBuffer volumeBBoxCubeVBO(QOpenGLBuffer::VertexBuffer);
	volumeBBoxCubeVBO.create();
	volumeBBoxCubeVBO.bind();
	volumeBBoxCubeVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
	volumeBBoxCubeVBO.allocate(&cubeVertices[0], 24 * sizeof(GLfloat));

	// generate index buffer object (vbo) listing vertex indices used for drawing
	QOpenGLBuffer volumeBBoxCubeIBO(QOpenGLBuffer::IndexBuffer);
	volumeBBoxCubeIBO.create();
	volumeBBoxCubeIBO.bind();
	volumeBBoxCubeIBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
	volumeBBoxCubeIBO.allocate(&cubeTriangleIndices[0], 36 * sizeof(GLint));

	// bind vertex and index data to shader attributes
	rayVolumeExitPosMapShader->bind();
	int vertexPositionAttribIndex = rayVolumeExitPosMapShader->attributeLocation("vertexPosition");
	rayVolumeExitPosMapShader->enableAttributeArray(vertexPositionAttribIndex); // enable bound vertex buffer at this index
	rayVolumeExitPosMapShader->setAttributeBuffer(vertexPositionAttribIndex, GL_FLOAT, 0, 3); // 3 components x,y,z

	// unbind buffers and shader
	volumeBBoxCubeVAO.release();
	volumeBBoxCubeIBO.release();
	volumeBBoxCubeVBO.release();
	rayVolumeExitPosMapShader->release();

}

void GLWidget::loadTransferFunction1DTex(const QString &fileName)
{
	// load transfer function 1D texture from image file
	transferFunction1DTex = new QOpenGLTexture(QOpenGLTexture::Target1D);
	transferFunction1DTex->create();
	transferFunction1DTex->setFormat(QOpenGLTexture::RGB8_UNorm);
	transferFunction1DTex->setData(QImage(fileName).convertToFormat(QImage::Format_RGB888));
	transferFunction1DTex->setWrapMode(QOpenGLTexture::Repeat);
	transferFunction1DTex->setMinificationFilter(QOpenGLTexture::Nearest);
	transferFunction1DTex->setMagnificationFilter(QOpenGLTexture::Nearest);
}

void GLWidget::initRayVolumeExitPosMapFramebuffer()
{
	// init framebuffer to hold a 2D texture for volume exit positions of orthogonal rays
	// texture is autogenerated on framebuffer creation and will be filled with data later.

	QOpenGLFramebufferObjectFormat fboFormat;
	fboFormat.setAttachment(QOpenGLFramebufferObject::Depth);
	fboFormat.setTextureTarget(GL_TEXTURE_2D);
	fboFormat.setInternalTextureFormat(GL_RGBA8);

	rayVolumeExitPosMapFramebuffer = new QOpenGLFramebufferObject(this->width(), this->height(), fboFormat);

}

void GLWidget::loadVolume3DTex()
{
	if (volume == nullptr) {
		return;
	}

	// fill volumeData into a 3D texture
	volume3DTex = new QOpenGLTexture(QOpenGLTexture::Target3D);
	volume3DTex->create();
	volume3DTex->setFormat(QOpenGLTexture::R32F);
	volume3DTex->setWrapMode(QOpenGLTexture::Repeat);
	volume3DTex->setMinificationFilter(QOpenGLTexture::Linear); // this is trilinear interpolation
	volume3DTex->setMagnificationFilter(QOpenGLTexture::Linear);
	volume3DTex->bind();
	// we can simply pass a pointer to the voxel vector since vocels only have a float member each it is same as a float array
	// note that for some reason we need to use internalformat GL_RGB (i.e. 3 channels) since GL_INTENSITY doesnt work.
	// yet the pixel data is still interpreted as a single intensity value due to GL_INTENSITY.
	glf->glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, volume->getWidth(), volume->getHeight(), volume->getDepth(), 0, GL_LUMINANCE, GL_FLOAT, volume->getVoxels());

}

void GLWidget::dataLoaded(Volume *volumeData)
{
	this->volume = volumeData;

	loadVolume3DTex();

}

void GLWidget::paintGL()
{
	glf->glClearColor(backgroundColor.red()/256.0f, backgroundColor.green()/256.0f, backgroundColor.blue()/256.0f, 1.0f);
	glf->glClear(GL_COLOR_BUFFER_BIT);

	if (!volume) {
		return;
	}

	glf->glEnable(GL_DEPTH_TEST);

	///////////////////////////////////////////////////////////////////////////////
	// FIRST PASS
	// generate ray volume exit position map later used to construct rays
	///////////////////////////////////////////////////////////////////////////////

	rayVolumeExitPosMapFramebuffer->bind();

	rayVolumeExitPosMapShader->bind();

	// draw volume cube back faces (front face culling enabled)
	// rayVolumeExitPosMapShader stores interpolated back face (ray exit) positions in framebuffer texture
	drawVolumeBBoxCube(GL_FRONT, rayVolumeExitPosMapShader);

	///////////////////////////////////////////////////////////////////////////////
	// SECOND PASS
	// calculate ray volume entry positions and together with exit position map
	// do raycasting from entry to exit position of each fragment
	///////////////////////////////////////////////////////////////////////////////

	QOpenGLFramebufferObject::bindDefault();

	raycastShader->bind();
	raycastShader->setUniformValue("screenDimensions", QVector2D(this->width(), this->height()));
	raycastShader->setUniformValue("numSamples", numSamples);
	raycastShader->setUniformValue("sampleRangeStart", sampleRangeStart);
	raycastShader->setUniformValue("sampleRangeEnd", sampleRangeEnd);
    if (technique == techniques::MIP) {
        raycastShader->setUniformValue("alphaTech", false);
        raycastShader->setUniformValue("avgTech", false);
    } else if (technique == techniques::ALPHA) {
        raycastShader->setUniformValue("alphaTech", true);
        raycastShader->setUniformValue("avgTech", false);
    } else if (technique == techniques::AVERAGE) {
        raycastShader->setUniformValue("alphaTech", false);
        raycastShader->setUniformValue("avgTech", true);
    }
    glActiveTexture(GL_TEXTURE0);
    transferFunction1DTex->bind(0); // bind texture to texture unit 0
    raycastShader->setUniformValue("transferFunction", 0); // bind shader uniform to texture unit 0
	raycastShader->setUniformValue("exitPositions", 1);
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, rayVolumeExitPosMapFramebuffer->texture());
	raycastShader->setUniformValue("volume", 2);
	volume3DTex->bind(2);

	// draw volume cube front faces (back face culling enabled)
	// raycastShader then uses interpolated front face (ray entry) positions with exit positions from first pass
	// to cast rays through the volume texture.
	// raycasting determines pixel intensity by sampling the volume voxel intensities
	// and mapping desired values to colors via the transfer function
	drawVolumeBBoxCube(GL_BACK, raycastShader);

	/*/ DEBUG VIEW FIRST PASS TEXTURE
	// blit framebuffer from first pass to default framebuffer
	// blit = bit block image transfer, combine bitmaps via boolean operation
	if (!QOpenGLFramebufferObject::hasOpenGLFramebufferBlit()) qWarning() << "Frame Buffer blitting not supported by local OpenGL implementation.";
	QOpenGLFramebufferObject::blitFramebuffer(0, rayVolumeExitPosMapFramebuffer); //*/

}

void GLWidget::drawVolumeBBoxCube(GLenum glFaceCullMode, QOpenGLShaderProgram *shader)
{
	glf->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// note that it doesnt matter to volume sampling how we transform the cube, since sampling rays are created between interpolated model space vertex positions
	modelMat.setToIdentity();
	modelMat.rotate(90 - volumeRotAngleX, QVector3D(1, 0, 0));
	QMatrix4x4 rotMatY;
	rotMatY.rotate(volumeRotAngleY, QVector3D(0, 1, 0));
	modelMat = rotMatY * modelMat;
	modelMat.translate(QVector3D(-0.5, -0.5, -0.5)); // move volume bounding box cube to center
	viewMat.setToIdentity();
	viewMat.lookAt(QVector3D(0.0f, 0.0f, viewOffset.z()), QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f)); // move camera slightly back and look at center
	viewMat.translate(viewOffset.x(), viewOffset.y(), 0.0f);
	projMat.setToIdentity();
	projMat.perspective(60, this->width()/this->height(), 0.01f, 1000.f);
	//projMat.ortho(-0.8f, 0.8f, -0.8f, 0.8f, 0.8f, 1000.f);

	shader->bind();
	int mvpMatUniformIndex = shader->uniformLocation("modelViewProjMat");
	shader->setUniformValue(mvpMatUniformIndex, projMat * viewMat * modelMat);

	glf->glEnable(GL_CULL_FACE);
	glf->glCullFace(glFaceCullMode);
	volumeBBoxCubeVAO.bind();
	glf->glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (GLuint *)NULL);
	glf->glDisable(GL_CULL_FACE);

}

void GLWidget::resizeGL(int w, int h)
{
	// TODO

	rayVolumeExitPosMapFramebuffer = new QOpenGLFramebufferObject(w, h, QOpenGLFramebufferObject::Depth);

}

void GLWidget::setNumSamples(int numSamples)
{
	this->numSamples = numSamples;
	repaint();
}

void GLWidget::setSampleRangeStart(double sampleRangeStart)
{
	this->sampleRangeStart = float(sampleRangeStart);
	repaint();
}

void GLWidget::setSampleRangeEnd(double sampleRangeEnd)
{
	this->sampleRangeEnd = float(sampleRangeEnd);
	repaint();
}

void GLWidget::setTechnique(techniques t)
{
    this->technique = t;
    repaint();
}

void GLWidget::loadTransferFunctionImage()
{
	QString fileName = QFileDialog::getOpenFileName(this, tr("Open Image"), "/home", tr("Image Files (*.png *.jpg)"));
	loadTransferFunction1DTex(fileName);

	repaint();
}

void GLWidget::mousePressEvent(QMouseEvent *event)
{
    this->tmpNumSamples = this->numSamples > 5 ? this->numSamples : this->tmpNumSamples;
	lastMousePos = event->pos();
}

void GLWidget::mouseMoveEvent(QMouseEvent *event)
{
    this->numSamples = 5; // SET SAMPLES LOWER FOR BETTER PERFORMANCE; RESET AFTER MOUSE RELEASE
	int dx = event->x() - lastMousePos.x();
	int dy = event->y() - lastMousePos.y();

	if (event->buttons() & Qt::LeftButton) {
		if (event->modifiers() & Qt::AltModifier) { // ZOOM
			viewOffset += QVector3D(0, 0, -dy/40.f);
			if (viewOffset.z() >  3.f) viewOffset.setZ(3.f);
			if (viewOffset.z() <  0.8f) viewOffset.setZ(0.8f);
		} else if (event->modifiers() & Qt::ControlModifier) { // PAN
			viewOffset += QVector3D(dx, -dy, 0.0f)/60.f;
			if (viewOffset.x() >  1) viewOffset.setX( 1);
			if (viewOffset.x() < -1) viewOffset.setX(-1);
			if (viewOffset.y() >  1) viewOffset.setY( 1);
			if (viewOffset.y() < -1) viewOffset.setY(-1);
		} else { // ROTATE
			if ((dy > 0 && volumeRotAngleX < 90) || (dy < 0 && volumeRotAngleX > -90)) {
				volumeRotAngleX += dy;
			}
			volumeRotAngleY += dx;
			if (volumeRotAngleX < -90) volumeRotAngleX = -90;
			if (volumeRotAngleX >  90) volumeRotAngleX =  90;
		}
	}
	lastMousePos = event->pos();

	repaint();
}

void GLWidget::mouseReleaseEvent(QMouseEvent *)
{
    this->numSamples = this->tmpNumSamples;
    repaint();
}

GLWidget::~GLWidget()
{
	// TODO
}
