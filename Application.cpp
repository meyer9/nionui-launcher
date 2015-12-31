/*
 Copyright (c) 2012-2015 Nion Company.
*/

#include <stdint.h>

#include "Application.h"
#include "DocumentWindow.h"
#if !USE_THRIFT
#include "PythonSupport.h"
#endif // !USE_THRIFT

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QMetaType>
#include <QtCore/QMimeData>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include <QtGui/QClipboard>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>
#include <QtGui/QPainter>

#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTextEdit>

#if USE_THRIFT
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
#endif

#include "LauncherConfig.h"

QString lastVisitedDir;

QString GetDirectory(const QString &path)
{
    QFileInfo info = QFileInfo(QDir::current(), path);
    if (info.exists() && info.isDir())
    return QDir::cleanPath(info.absoluteFilePath());
    info.setFile(info.absolutePath());
    if (info.exists() && info.isDir())
    return info.absoluteFilePath();
    return QString();
}

QString WorkingDirectory(const QString &path)
{
    if (!path.isEmpty()) {
        QString directory = GetDirectory(path);
        if (!directory.isEmpty())
        return directory;
    }
    QString directory = GetDirectory(lastVisitedDir);
    if (!directory.isEmpty())
    return directory;
    return QDir::currentPath();
}

QString InitialSelection(const QString &path)
{
    if (!path.isEmpty()) {
        QFileInfo info(path);
        if (!info.isDir())
        return info.fileName();
    }
    return QString();
}

QString GetSaveFileName(QWidget *parent,
                        const QString &caption,
                        const QString &dir,
                        const QString &filter,
                        QString *selectedFilter = NULL,
                        QDir *selectedDirectory = NULL)
{
    // create a qt dialog
    QFileDialog dialog(parent, caption, WorkingDirectory(dir), filter);
    dialog.selectFile(InitialSelection(dir));
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    if (selectedFilter && !selectedFilter->isEmpty())
    dialog.selectNameFilter(*selectedFilter);
    if (dialog.exec() == QDialog::Accepted) {
        if (selectedFilter)
        *selectedFilter = dialog.selectedNameFilter();
        if (selectedDirectory)
        *selectedDirectory = dialog.directory();
        return dialog.selectedFiles().value(0);
    }
    return QString();
}

QString GetOpenFileName(QWidget *parent,
                        const QString &caption,
                        const QString &dir,
                        const QString &filter,
                        QString *selectedFilter = NULL,
                        QDir *selectedDirectory = NULL)
{
    // create a qt dialog
    QFileDialog dialog(parent, caption, WorkingDirectory(dir), filter);
    dialog.selectFile(InitialSelection(dir));
    dialog.setFileMode(QFileDialog::ExistingFile);
    if (selectedFilter && !selectedFilter->isEmpty())
    dialog.selectNameFilter(*selectedFilter);
    if (dialog.exec() == QDialog::Accepted) {
        if (selectedFilter)
        *selectedFilter = dialog.selectedNameFilter();
        if (selectedDirectory)
        *selectedDirectory = dialog.directory();
        return dialog.selectedFiles().value(0);
    }
    return QString();
}

QString GetExistingDirectory(QWidget *parent,
                             const QString &caption,
                             const QString &dir,
                             QDir *selectedDirectory = NULL)
{
    // create a qt dialog
    QFileDialog dialog(parent, caption, WorkingDirectory(dir));
    dialog.selectFile(InitialSelection(dir));
    dialog.setFileMode(QFileDialog::DirectoryOnly);  // also QFileDialog::Directory
    if (dialog.exec() == QDialog::Accepted) {
        if (selectedDirectory)
        *selectedDirectory = dialog.directory();
        return dialog.selectedFiles().value(0);
    }
    return QString();
}

QStringList GetOpenFileNames(QWidget *parent,
                             const QString &caption,
                             const QString &dir,
                             const QString &filter,
                             QString *selectedFilter = NULL,
                             QDir *selectedDirectory = NULL)
{
    // create a qt dialog
    QFileDialog dialog(parent, caption, WorkingDirectory(dir), filter);
    dialog.selectFile(InitialSelection(dir));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    if (selectedFilter && !selectedFilter->isEmpty())
    dialog.selectNameFilter(*selectedFilter);
    if (dialog.exec() == QDialog::Accepted) {
        if (selectedFilter)
        *selectedFilter = dialog.selectedNameFilter();
        if (selectedDirectory)
        *selectedDirectory = dialog.directory();
        return dialog.selectedFiles();
    }
    return QStringList();
}

QSizePolicy::Policy ParseSizePolicy(const QString &policy_str)
{
    if (policy_str.compare("maximum", Qt::CaseInsensitive) == 0)
        return QSizePolicy::Maximum;
    if (policy_str.compare("minimum", Qt::CaseInsensitive) == 0)
        return QSizePolicy::Minimum;
    if (policy_str.compare("expanding", Qt::CaseInsensitive) == 0)
        return QSizePolicy::Expanding;
    return QSizePolicy::Fixed;
}

Qt::ScrollBarPolicy ParseScrollBarPolicy(const QString &policy_str)
{
    if (policy_str.compare("off", Qt::CaseInsensitive) == 0)
        return Qt::ScrollBarAlwaysOff;
    if (policy_str.compare("on", Qt::CaseInsensitive) == 0)
        return Qt::ScrollBarAlwaysOn;
    else
        return Qt::ScrollBarAsNeeded;
}

#if !USE_THRIFT
#define LOG_EXCEPTION(ctx) qDebug() << "EXCEPTION";

template <typename T>
QList<T> reversed( const QList<T> & in ) {
    QList<T> result;
    std::reverse_copy( in.begin(), in.end(), std::back_inserter( result ) );
    return result;
}

#ifdef Py_UNICODE_WIDE
    #if PY_MAJOR_VERSION >= 3
        #define Py_UNICODE_to_QString(x) QString::fromWCharArray(x)
    #else
        #define Py_UNICODE_to_QString(x) QString::fromUcs4(x)
    #endif
#else
    #define Py_UNICODE_to_QString(x) QString::fromWCharArray(x)
#endif

//----

static PyObject *Action_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyAction *action = Unwrap<PyAction>(obj0);
    if (action == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    action->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Action_create(PyObject * /*self*/, PyObject *args)
{
    Q_UNUSED(args)

    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *title_u = NULL;
    char *key_sequence_c = NULL;
    char *role_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Ouzz", &obj0, &title_u, &key_sequence_c, &role_c))
        return NULL;

    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    PyAction *action = new PyAction(document_window);
    action->setText(Py_UNICODE_to_QString(title_u));

    if (key_sequence_c)
    {
        if (strcmp(key_sequence_c, "new") == 0)
            action->setShortcut(QKeySequence::New);
        else if (strcmp(key_sequence_c, "open") == 0)
            action->setShortcut(QKeySequence::Open);
        else if (strcmp(key_sequence_c, "close") == 0)
            action->setShortcut(QKeySequence::Close);
        else if (strcmp(key_sequence_c, "save") == 0)
            action->setShortcut(QKeySequence::Save);
        else if (strcmp(key_sequence_c, "save-as") == 0)
            action->setShortcut(QKeySequence::SaveAs);
        else if (strcmp(key_sequence_c, "quit") == 0)
            action->setShortcut(QKeySequence::Quit);
        else if (strcmp(key_sequence_c, "undo") == 0)
            action->setShortcut(QKeySequence::Undo);
        else if (strcmp(key_sequence_c, "redo") == 0)
            action->setShortcut(QKeySequence::Redo);
        else if (strcmp(key_sequence_c, "copy") == 0)
            action->setShortcut(QKeySequence::Copy);
        else if (strcmp(key_sequence_c, "paste") == 0)
            action->setShortcut(QKeySequence::Paste);
        else if (strcmp(key_sequence_c, "delete") == 0)
            action->setShortcut(QKeySequence::Delete);
        else if (strcmp(key_sequence_c, "select-all") == 0)
            action->setShortcut(QKeySequence::SelectAll);
        else if (strcmp(key_sequence_c, "help") == 0)
            action->setShortcut(QKeySequence::HelpContents);
        else
            action->setShortcut(QKeySequence(key_sequence_c));
    }

    if (role_c)
    {
        if (strcmp(role_c, "preferences") == 0)
            action->setMenuRole(QAction::PreferencesRole);
        else if (strcmp(role_c, "about") == 0)
            action->setMenuRole(QAction::AboutRole);
        else if (strcmp(role_c, "application") == 0)
            action->setMenuRole(QAction::ApplicationSpecificRole);
        else if (strcmp(role_c, "quit") == 0)
            action->setMenuRole(QAction::QuitRole);
    }

    return WrapQObject(action);
}

static PyObject *Action_setChecked(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    bool checked = false;
    if (!PythonSupport::instance()->parse()(args, "Ob", &obj0, &checked))
        return NULL;

    PyAction *action = Unwrap<PyAction>(obj0);
    if (action == NULL)
        return NULL;

    action->setCheckable(checked);
    action->setChecked(checked);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Action_setEnabled(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    bool enabled = false;
    if (!PythonSupport::instance()->parse()(args, "Ob", &obj0, &enabled))
        return NULL;

    PyAction *action = Unwrap<PyAction>(obj0);
    if (action == NULL)
        return NULL;

    action->setEnabled(enabled);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Action_setTitle(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *title_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &title_u))
        return NULL;

    PyAction *action = Unwrap<PyAction>(obj0);
    if (action == NULL)
        return NULL;

    action->setText(Py_UNICODE_to_QString(title_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Application_close(PyObject * /*self*/, PyObject *args)
{
    Q_UNUSED(args)

    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    qApp->closeAllWindows();
    qApp->quit();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Canvas_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyCanvas *canvas = Unwrap<PyCanvas>(obj0);
    if (canvas == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    canvas->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Canvas_draw(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    PyObject *obj2 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OOO", &obj0, &obj1, &obj2))
        return NULL;

    PyCanvas *canvas = Unwrap<PyCanvas>(obj0);
    if (canvas == NULL)
        return NULL;

//    PyDrawingContextStorage *storage = Unwrap<PyDrawingContextStorage>(obj2);

    QVariantList raw_commands = PyObjectToQVariant(obj1).toList();

    {
        Python_ThreadAllow thread_allow;

        QList<CanvasDrawingCommand> drawing_commands;

        Q_FOREACH(const QVariant &raw_command_variant, raw_commands)
        {
            QVariantList raw_command = raw_command_variant.toList();
            CanvasDrawingCommand drawing_command;
            drawing_command.command = raw_command[0].toString();
            drawing_command.arguments = raw_command.mid(1);
            drawing_commands.append(drawing_command);
        }

        canvas->setCommands(drawing_commands);
    }

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Canvas_grabMouse(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyCanvas *canvas = Unwrap<PyCanvas>(obj0);
    if (canvas == NULL)
        return NULL;

    canvas->grabMouse0();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Canvas_releaseMouse(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyCanvas *canvas = Unwrap<PyCanvas>(obj0);
    if (canvas == NULL)
        return NULL;

    canvas->releaseMouse0();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Canvas_setCursorShape(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *shape_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &shape_c))
        return NULL;

    PyCanvas *canvas = Unwrap<PyCanvas>(obj0);
    if (canvas == NULL)
        return NULL;

    Qt::CursorShape cursor_shape = Qt::ArrowCursor;

    if (strcmp(shape_c, "arrow") == 0)
        cursor_shape = Qt::ArrowCursor;
    else if (strcmp(shape_c, "up_arrow") == 0)
        cursor_shape = Qt::UpArrowCursor;
    else if (strcmp(shape_c, "cross") == 0)
        cursor_shape = Qt::CrossCursor;
    else if (strcmp(shape_c, "wait") == 0)
        cursor_shape = Qt::WaitCursor;
    else if (strcmp(shape_c, "ibeam") == 0)
        cursor_shape = Qt::IBeamCursor;
    else if (strcmp(shape_c, "wait") == 0)
        cursor_shape = Qt::WaitCursor;
    else if (strcmp(shape_c, "size_vertical") == 0)
        cursor_shape = Qt::SizeVerCursor;
    else if (strcmp(shape_c, "size_horizontal") == 0)
        cursor_shape = Qt::SizeHorCursor;
    else if (strcmp(shape_c, "size_backward_diagonal") == 0)
        cursor_shape = Qt::SizeBDiagCursor;
    else if (strcmp(shape_c, "size_forward_diagonal") == 0)
        cursor_shape = Qt::SizeFDiagCursor;
    else if (strcmp(shape_c, "blank") == 0)
        cursor_shape = Qt::BlankCursor;
    else if (strcmp(shape_c, "split_vertical") == 0)
        cursor_shape = Qt::SplitVCursor;
    else if (strcmp(shape_c, "split_horizontal") == 0)
        cursor_shape = Qt::SplitHCursor;
    else if (strcmp(shape_c, "pointing_hand") == 0)
        cursor_shape = Qt::PointingHandCursor;
    else if (strcmp(shape_c, "forbidden") == 0)
        cursor_shape = Qt::ForbiddenCursor;
    else if (strcmp(shape_c, "hand") == 0)
        cursor_shape = Qt::OpenHandCursor;
    else if (strcmp(shape_c, "closed_hand") == 0)
        cursor_shape = Qt::ClosedHandCursor;
    else if (strcmp(shape_c, "question") == 0)
        cursor_shape = Qt::WhatsThisCursor;
    else if (strcmp(shape_c, "busy") == 0)
        cursor_shape = Qt::BusyCursor;
    else if (strcmp(shape_c, "move") == 0)
        cursor_shape = Qt::DragMoveCursor;
    else if (strcmp(shape_c, "copy") == 0)
        cursor_shape = Qt::DragCopyCursor;
    else if (strcmp(shape_c, "link") == 0)
        cursor_shape = Qt::DragLinkCursor;

    canvas->setCursor(QCursor(cursor_shape));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Canvas_setFocusPolicy(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    int policy;

    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &policy))
        return NULL;

    PyCanvas *canvas = Unwrap<PyCanvas>(obj0);
    if (canvas == NULL)
        return NULL;

    canvas->setFocusPolicy((Qt::FocusPolicy)policy);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *CheckBox_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyCheckBox *check_box = Unwrap<PyCheckBox>(obj0);
    if (check_box == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    check_box->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *CheckBox_getCheckState(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyCheckBox *check_box = Unwrap<PyCheckBox>(obj0);
    if (check_box == NULL)
        return NULL;

    QStringList state_names;
    state_names << "unchecked" << "partial" << "checked";

    return PythonSupport::instance()->build()("s", state_names[check_box->checkState()].toUtf8().data());
}

static PyObject *CheckBox_getIsTristate(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyCheckBox *check_box = Unwrap<PyCheckBox>(obj0);
    if (check_box == NULL)
        return NULL;

    return PythonSupport::instance()->build()("b", check_box->isTristate());
}

static PyObject *CheckBox_setCheckState(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    char *check_state_str = NULL;

    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &check_state_str))
        return NULL;

    PyCheckBox *check_box = Unwrap<PyCheckBox>(obj0);
    if (check_box == NULL)
        return NULL;

    if (strcmp(check_state_str, "checked") == 0)
        check_box->setCheckState(Qt::Checked);
    else if (strcmp(check_state_str, "partial") == 0)
        check_box->setCheckState(Qt::Checked);
    else
        check_box->setCheckState(Qt::Unchecked);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *CheckBox_setIsTristate(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    bool tristate = false;

    if (!PythonSupport::instance()->parse()(args, "Ob", &obj0, &tristate))
        return NULL;

    PyCheckBox *check_box = Unwrap<PyCheckBox>(obj0);
    if (check_box == NULL)
        return NULL;

    check_box->setTristate(tristate);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *CheckBox_setText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
        return NULL;

    PyCheckBox *check_box = Unwrap<PyCheckBox>(obj0);
    if (check_box == NULL)
        return NULL;

    check_box->setText(Py_UNICODE_to_QString(text_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Clipboard_clear(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    QClipboard *clipboard = QApplication::clipboard();

    clipboard->clear();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Clipboard_mimeData(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    QClipboard *clipboard = QApplication::clipboard();

    clipboard->clear();

    const QMimeData *mime_data = clipboard->mimeData();

    return WrapQObject((QMimeData *)mime_data);
}

static PyObject *Clipboard_setMimeData(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    QMimeData *mime_data = Unwrap<QMimeData>(obj0);
    if (mime_data == NULL)
        return NULL;

    QClipboard *clipboard = QApplication::clipboard();

    clipboard->setMimeData(mime_data);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Clipboard_setText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "u", &text_u))
        return NULL;

    QClipboard *clipboard = QApplication::clipboard();

    clipboard->setText(Py_UNICODE_to_QString(text_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Clipboard_text(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    QClipboard *clipboard = QApplication::clipboard();

    QString text = clipboard->text();

    return PythonSupport::instance()->build()("s", text.toUtf8().data());
}

static PyObject *ComboBox_addItem(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
        return NULL;

    PyComboBox *combobox = Unwrap<PyComboBox>(obj0);
    if (combobox == NULL)
        return NULL;

    combobox->addItem(Py_UNICODE_to_QString(text_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ComboBox_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyComboBox *combobox = Unwrap<PyComboBox>(obj0);
    if (combobox == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    combobox->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ComboBox_getCurrentText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyComboBox *combobox = Unwrap<PyComboBox>(obj0);
    if (combobox == NULL)
        return NULL;

    QString currentText = combobox->currentText();

    return PythonSupport::instance()->build()("s", currentText.toUtf8().data());
}

static PyObject *ComboBox_removeAllItems(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyComboBox *combobox = Unwrap<PyComboBox>(obj0);
    if (combobox == NULL)
        return NULL;

    while (combobox->count())
        combobox->removeItem(0);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ComboBox_setCurrentText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
    return NULL;

    PyComboBox *combobox = Unwrap<PyComboBox>(obj0);
    if (combobox == NULL)
    return NULL;

    combobox->setCurrentText(Py_UNICODE_to_QString(text_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Console_connect(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the console
    Console *console = Unwrap<Console>(obj0);
    if (console == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    console->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Console_insertFromStringList(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the console
    Console *console = Unwrap<Console>(obj0);
    if (console == NULL)
        return NULL;

    // Send the strings to the console
    QVariant string_list_v = PyObjectToQVariant(obj1);
    console->insertFromStringList(string_list_v.toStringList());

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Core_getFontMetrics(PyObject * /*self*/, PyObject *args)
{
    char *font_c = NULL;
    char *text_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "sz", &font_c, &text_c))
        return NULL;

    QString text = (text_c != NULL) ? text_c : QString();

    QFont font;
    QString family;
    Q_FOREACH(const QString &font_part, QString(font_c).simplified().split(" "))
    {
        if (font_part == "italic")
            font.setStyle(QFont::StyleItalic);
        if (font_part == "oblique")
            font.setStyle(QFont::StyleOblique);
        if (font_part == "small-caps")
            font.setCapitalization(QFont::SmallCaps);
        if (font_part == "bold")
            font.setWeight(QFont::Bold);
        if (font_part.endsWith("px") && font_part.left(font_part.length() - 2).toInt() > 0)
            font.setPixelSize(font_part.left(font_part.length() - 2).toInt());
        family = font_part;
    }
    font.setFamily(family);

    QFontMetrics font_metrics(font);

    QVariantList result;

    result << font_metrics.width(text);
    result << font_metrics.height();
    result << font_metrics.ascent();
    result << font_metrics.descent();
    result << font_metrics.leading();

    return QVariantToPyObject(result);
}

static PyObject *Core_getLocation(PyObject * /*self*/, PyObject *args)
{
    char *location_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "s", &location_c))
        return NULL;

    QString location_str(location_c);

    QStandardPaths::StandardLocation location = QStandardPaths::DocumentsLocation;
    if (location_str == "data")
        location = QStandardPaths::DataLocation;
    else if (location_str == "documents")
        location = QStandardPaths::DocumentsLocation;
    else if (location_str == "temporary")
        location = QStandardPaths::TempLocation;
    QDir dir(QStandardPaths::writableLocation(location));
    QString data_location;
    data_location = dir.absolutePath();
    QDir().mkpath(data_location);

    return PythonSupport::instance()->build()("s", data_location.toUtf8().data());
}

static PyObject *Core_out(PyObject * /*self*/, PyObject *args)
{
    Py_UNICODE *output_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "u", &output_u))
        return NULL;

    QString output = Py_UNICODE_to_QString(output_u);

    {
        Python_ThreadAllow thread_allow;

        output = output.trimmed();
        if (!output.isEmpty())
        {
            QTextStream cout(stdout);
            cout << (const char *)(output.toUtf8().data()) << endl;
        }
    }

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Core_pathToURL(PyObject * /*self*/, PyObject *args)
{
    Py_UNICODE *path_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "u", &path_u))
        return NULL;

    QUrl url = QUrl::fromLocalFile(Py_UNICODE_to_QString(path_u));
    QString url_string = url.toString();

    return PythonSupport::instance()->build()("s", url_string.toUtf8().data());
}

static PyObject *Core_readImageToBinary(PyObject * /*self*/, PyObject *args)
{
    Py_UNICODE *filename_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "u", &filename_u))
        return NULL;

    // Read the image
    QImageReader reader(Py_UNICODE_to_QString(filename_u));
    if (reader.canRead())
    {
        Python_ThreadAllow thread_allow;

        QImage image = reader.read();

        if (image.format() != QImage::Format_ARGB32_Premultiplied)
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        thread_allow.release();

        return PythonSupport::instance()->arrayFromImage(image);
    }

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Core_URLToPath(PyObject * /*self*/, PyObject *args)
{
    char *url_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "s", &url_c))
        return NULL;

    QUrl url(url_c);

    QString file_path = url.toLocalFile();

    return PythonSupport::instance()->build()("s", file_path.toUtf8().data());
}

static PyObject *Core_writeBinaryToImage(PyObject * /*self*/, PyObject *args)
{
    int w = 0;
    int h = 0;
    PyObject *obj0 = NULL;
    Py_UNICODE *filename_u = NULL;
    char *format_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "iiOus", &w, &h, &obj0, &filename_u, &format_c))
        return NULL;

    QImage image = PythonSupport::instance()->imageFromArray(obj0);

    if (image.isNull())
        return NULL;

    // Write the image
    QImageWriter writer(Py_UNICODE_to_QString(filename_u), format_c);

    if (writer.canWrite())
        writer.write(image);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DockWidget_getToggleAction(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the dock widget
    QDockWidget *dock_widget = Unwrap<QDockWidget>(obj0);
    if (dock_widget == NULL)
        return NULL;

    return WrapQObject(dock_widget->toggleViewAction());
}

static PyObject *DocumentWindow_addDockWidget(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    char *identifier_c = NULL;
    Py_UNICODE *title_u = NULL;
    PyObject *obj2 = NULL;
    char *position_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "OOsuOs", &obj0, &obj1, &identifier_c, &title_u, &obj2, &position_c))
        return NULL;

    // Grab the document window
    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    // Grab the widget to add
    QWidget *widget = Unwrap<QWidget>(obj1);
    if (widget == NULL)
        return NULL;

    // Grab the allowed positions
    QVariant py_allowed_positions = PyObjectToQVariant(obj2);
    QStringList allowed_positions = py_allowed_positions.toStringList();

    QMap<QString, Qt::DockWidgetArea> mapping;
    mapping["top"] = Qt::TopDockWidgetArea;
    mapping["left"] = Qt::LeftDockWidgetArea;
    mapping["bottom"] = Qt::BottomDockWidgetArea;
    mapping["right"] = Qt::RightDockWidgetArea;
    mapping["all"] = Qt::AllDockWidgetAreas;
    mapping["none"] = Qt::NoDockWidgetArea;

    Qt::DockWidgetAreas allowed_positions_mask = 0;
    Q_FOREACH(const QString &allowed_position, allowed_positions)
    {
        allowed_positions_mask |= mapping[allowed_position];
    }

    QDockWidget *dock_widget = new QDockWidget(Py_UNICODE_to_QString(title_u));
    dock_widget->setAllowedAreas(allowed_positions_mask);
    dock_widget->setWidget(widget);
    dock_widget->setObjectName(identifier_c);
    document_window->addDockWidget(mapping[position_c], dock_widget);

    return WrapQObject(dock_widget);
}

static PyObject *DocumentWindow_addMenu(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *title_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &title_u))
        return NULL;

    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    PyMenu *menu = new PyMenu();
    menu->setTitle(Py_UNICODE_to_QString(title_u));

    document_window->menuBar()->addMenu(menu);

    return WrapQObject(menu);
}

static PyObject *DocumentWindow_close(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    document_window->close();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    document_window->setPyObject(py_object);
    document_window->initialize();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_create(PyObject * /*self*/, PyObject *args)
{
    Q_UNUSED(args)

    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    Py_UNICODE *title_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "Z", &title_u))
        return NULL;

    QString title = title_u ? Py_UNICODE_to_QString(title_u) : QString();

    DocumentWindow *document_window = new DocumentWindow(title);

    return WrapQObject(document_window);
}

static PyObject *DocumentWindow_getFilePath(PyObject * /*self*/, PyObject *args)
{
    // simple wrapper for the QtFile dialogs. This way plugins can
    // display file dialogs when needed.
    // Args are document window, mode ("save" | "load" | "loadmany") , caption, dir, filter
    // returns the path or an empty string if the dialog was cancelled
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }
    PyObject *obj0 = NULL;
    char *mode_c = NULL;
    Py_UNICODE *caption_u = NULL;
    Py_UNICODE *dir_u = NULL;
    Py_UNICODE *filter_u = NULL;
    Py_UNICODE *selected_filter_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "Osuuuu", &obj0, &mode_c, &caption_u, &dir_u, &filter_u, &selected_filter_u))
        return NULL;

    DocumentWindow *document_window = PythonSupport::instance()->isNone(obj0) ? NULL : Unwrap<DocumentWindow>(obj0);

    if (strcmp(mode_c, "save")==0)
    {
		QString selected_filter = Py_UNICODE_to_QString(selected_filter_u);
        QDir selected_dir;

        QString ret;
        {
            Python_ThreadAllow thread_allow;
            ret = GetSaveFileName(document_window, Py_UNICODE_to_QString(caption_u), Py_UNICODE_to_QString(dir_u), Py_UNICODE_to_QString(filter_u), &selected_filter, &selected_dir);
        }
        QVariantList result;
        result << ret;
        result << selected_filter;
        result << selected_dir.absolutePath();

        return QVariantToPyObject(result);
    }
    else if (strcmp(mode_c, "load")==0)
    {
		QString selected_filter = Py_UNICODE_to_QString(selected_filter_u);
        QDir selected_dir;

        QString ret;
        {
            Python_ThreadAllow thread_allow;
            ret = GetOpenFileName(document_window, Py_UNICODE_to_QString(caption_u), Py_UNICODE_to_QString(dir_u), Py_UNICODE_to_QString(filter_u), &selected_filter, &selected_dir);
        }

        QVariantList result;
        result << ret;
        result << selected_filter;
        result << selected_dir.absolutePath();

        return QVariantToPyObject(result);
    }
    else if (strcmp(mode_c, "directory") == 0)
    {
        QDir selected_dir;
        QDir::setCurrent(Py_UNICODE_to_QString(dir_u));
        QString directory;
        {
            Python_ThreadAllow thread_allow;
            directory = GetExistingDirectory(document_window, Py_UNICODE_to_QString(caption_u), Py_UNICODE_to_QString(dir_u), &selected_dir);
        }

        QVariantList result;
        result << directory;
        result << QString();
        result << selected_dir.absolutePath();

        return QVariantToPyObject(result);
    }
    else if (strcmp(mode_c, "loadmany") == 0)
    {
		QString selected_filter = Py_UNICODE_to_QString(selected_filter_u);
        QDir selected_dir;

        QStringList file_names;
        {
            Python_ThreadAllow thread_allow;
            file_names = GetOpenFileNames(document_window, Py_UNICODE_to_QString(caption_u), Py_UNICODE_to_QString(dir_u), Py_UNICODE_to_QString(filter_u), &selected_filter, &selected_dir);
        }

        QVariantList result;
        result << file_names;
        result << selected_filter;
        result << selected_dir.absolutePath();

        return QVariantToPyObject(result);
    }

    // error
    return NULL;
}

static PyObject *DocumentWindow_insertMenu(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *title_u = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OuO", &obj0, &title_u, &obj1))
        return NULL;

    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    PyMenu *before_menu = Unwrap<PyMenu>(obj1);
    if (before_menu == NULL)
        return NULL;

    PyMenu *menu = new PyMenu();
    menu->setTitle(Py_UNICODE_to_QString(title_u));

    document_window->menuBar()->insertMenu(before_menu->menuAction(), menu);

    return WrapQObject(menu);
}

static PyObject *DocumentWindow_removeDockWidget(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the document window
    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    // Grab the dock widget
    QDockWidget *dock_widget = Unwrap<QDockWidget>(obj1);
    if (dock_widget == NULL)
        return NULL;

    document_window->removeDockWidget(dock_widget);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_restore(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    char *geometry_c = NULL;
    char *state_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Ozz", &obj0, &geometry_c, &state_c))
        return NULL;

    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    // state, then geometry, otherwise the size isn't handled right. ugh.

    if (state_c)
        document_window->restoreState(QByteArray::fromHex(state_c));
    if (geometry_c)
        document_window->restoreGeometry(QByteArray::fromHex(geometry_c));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_setCentralWidget(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the document window
    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    // Grab the widget
    QWidget *widget = Unwrap<QWidget>(obj1);
    if (widget == NULL)
        return NULL;

    document_window->setCentralWidget(widget);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_setPosition(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int gx, gy;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &gx, &gy))
        return NULL;

    // Grab the document window
    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    document_window->move(QPoint(gx, gy));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_setSize(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int width, height;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &width, &height))
        return NULL;

    // Grab the document window
    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    document_window->resize(QSize(width, height));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_setTitle(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *title_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &title_u))
        return NULL;

    // Grab the document window
    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    document_window->setWindowTitle(Py_UNICODE_to_QString(title_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_show(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    char *window_style_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &window_style_c))
        return NULL;

    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    QStringList window_styles;
    window_styles << "window" << "dialog" << "popup";

    if (!window_styles.contains(window_style_c))
        return NULL;

    QString window_style = QString::fromStdString(window_style_c);

    if (window_style == "dialog")
        document_window->setWindowFlags(Qt::Dialog);
    else if (window_style == "popup")
        document_window->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);

    document_window->show();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DocumentWindow_tabifyDockWidgets(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    PyObject *obj2 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OOO", &obj0, &obj1, &obj2))
        return NULL;

    // Grab the document window
    DocumentWindow *document_window = Unwrap<DocumentWindow>(obj0);
    if (document_window == NULL)
        return NULL;

    // Grab the first widget
    QDockWidget *dock_widget1 = Unwrap<QDockWidget>(obj1);
    if (dock_widget1 == NULL)
        return NULL;

    // Grab the widget
    QDockWidget *dock_widget2 = Unwrap<QDockWidget>(obj2);
    if (dock_widget2 == NULL)
        return NULL;

    document_window->tabifyDockWidget(dock_widget1, dock_widget2);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Drag_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    Drag *drag = Unwrap<Drag>(obj0);
    if (drag == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    drag->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Drag_create(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
    return NULL;

    QMimeData *mime_data = Unwrap<QMimeData>(obj1);
    if (mime_data == NULL)
        return NULL;

    Drag *drag = new Drag(widget);

    drag->setMimeData(mime_data);

    return WrapQObject(drag);
}

static PyObject *Drag_exec(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    Drag *drag = Unwrap<Drag>(obj0);
    if (drag == NULL)
        return NULL;

    QTimer::singleShot(0, drag, SLOT(execute()));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Drag_setThumbnail(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int w, h;
    PyObject *obj1 = NULL;
    int x, y;
    if (!PythonSupport::instance()->parse()(args, "OiiOii", &obj0, &w, &h, &obj1, &x, &y))
        return NULL;

    Drag *drag = Unwrap<Drag>(obj0);
    if (drag == NULL)
        return NULL;

    if (!PythonSupport::instance()->isNone(obj1))
    {
        QImage image = PythonSupport::instance()->imageFromArray(obj1);

        if (image.isNull())
            return NULL;

        drag->setPixmap(QPixmap::fromImage(image));
        drag->setHotSpot(QPoint(x, y));
    }

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DrawingContext_drawCommands(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the drawing context
    PyDrawingContext *drawing_context = Unwrap<PyDrawingContext>(obj0);
    if (drawing_context == NULL)
        return NULL;

    QList<CanvasDrawingCommand> drawing_commands;

    QVariantList raw_commands = PyObjectToQVariant(obj1).toList();

    {
        Python_ThreadAllow thread_allow;

        Q_FOREACH(const QVariant &raw_command_variant, raw_commands)
        {
            QVariantList raw_command = raw_command_variant.toList();
            CanvasDrawingCommand drawing_command;
            drawing_command.command = raw_command[0].toString();
            drawing_command.arguments = raw_command.mid(1);
            drawing_commands.append(drawing_command);
        }

        drawing_context->paintCommands(drawing_commands);
    }

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *DrawingContext_paintRGBA(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    int width = 0;
    int height = 0;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &width, &height))
        return NULL;

    QVariantList raw_commands = PyObjectToQVariant(obj0).toList();

    Python_ThreadAllow thread_allow;

    QImage image((int)width, (int)height, QImage::Format_ARGB32);

    {
        QPainter painter(&image);
        PaintImageCache image_cache;
        QList<CanvasDrawingCommand> drawing_commands;
        Q_FOREACH(const QVariant &raw_command_variant, raw_commands)
        {
            QVariantList raw_command = raw_command_variant.toList();
            CanvasDrawingCommand drawing_command;
            drawing_command.command = raw_command[0].toString();
            drawing_command.arguments = raw_command.mid(1);
            drawing_commands.append(drawing_command);
        }
        PaintCommands(painter, drawing_commands, &image_cache);
    }

    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    thread_allow.release();

    return PythonSupport::instance()->arrayFromImage(image);
}

static PyObject *ItemModel_beginInsertRows(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int first_index = -1;
    int last_index = -1;
    int parent_row = -1;
    int parent_item_id = -1;
    if (!PythonSupport::instance()->parse()(args, "Oiiii", &obj0, &first_index, &last_index, &parent_row, &parent_item_id))
        return NULL;

    // Grab the item controller (a python object)
    ItemModel *py_item_model = Unwrap<ItemModel>(obj0);
    if (py_item_model == NULL)
        return NULL;

    py_item_model->beginInsertRowsInParent(first_index, last_index, parent_row, parent_item_id);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ItemModel_beginRemoveRows(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int first_index = -1;
    int last_index = -1;
    int parent_row = -1;
    int parent_item_id = -1;
    if (!PythonSupport::instance()->parse()(args, "Oiiii", &obj0, &first_index, &last_index, &parent_row, &parent_item_id))
        return NULL;

    // Grab the item controller (a python object)
    ItemModel *py_item_model = Unwrap<ItemModel>(obj0);
    if (py_item_model == NULL)
        return NULL;

    py_item_model->beginRemoveRowsInParent(first_index, last_index, parent_row, parent_item_id);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ItemModel_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
    return NULL;

    // Grab the item controller (a python object)
    ItemModel *py_item_model = Unwrap<ItemModel>(obj0);
    if (py_item_model == NULL)
    return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    py_item_model->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ItemModel_create(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the role names
    QVariant py_role_names = PyObjectToQVariant(obj0);
    QStringList role_names = py_role_names.toStringList();

    ItemModel *py_item_model = new ItemModel(role_names);

    return WrapQObject(py_item_model);
}

static PyObject *ItemModel_dataChanged(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int index = -1;
    int parent_row = -1;
    int parent_item_id = -1;
    if (!PythonSupport::instance()->parse()(args, "Oiii", &obj0, &index, &parent_row, &parent_item_id))
        return NULL;

    // Grab the item controller (a python object)
    ItemModel *py_item_model = Unwrap<ItemModel>(obj0);
    if (py_item_model == NULL)
        return NULL;

    py_item_model->dataChangedInParent(index, parent_row, parent_item_id);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ItemModel_destroy(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the item controller (a python object)
    ItemModel *py_item_model = Unwrap<ItemModel>(obj0);
    if (py_item_model == NULL)
        return NULL;

    py_item_model->deleteLater();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ItemModel_endInsertRow(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the item controller (a python object)
    ItemModel *py_item_model = Unwrap<ItemModel>(obj0);
    if (py_item_model == NULL)
        return NULL;

    py_item_model->endInsertRowsInParent();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ItemModel_endRemoveRow(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the item controller (a python object)
    ItemModel *py_item_model = Unwrap<ItemModel>(obj0);
    if (py_item_model == NULL)
        return NULL;

    py_item_model->endRemoveRowsInParent();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Label_setText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
        return NULL;

    QLabel *label = Unwrap<QLabel>(obj0);
    if (label == NULL)
        return NULL;

    QString text = Py_UNICODE_to_QString(text_u);
    if (text.length() > 0)
        label->setText(text);
    else
        label->clear();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *LineEdit_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    line_edit->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *LineEdit_getEditable(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    return PythonSupport::instance()->build()("b", !line_edit->isReadOnly());
}

static PyObject *LineEdit_getPlaceholderText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    return PythonSupport::instance()->build()("s", line_edit->placeholderText().toUtf8().data());
}

static PyObject *LineEdit_getText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    return PythonSupport::instance()->build()("s", line_edit->text().toUtf8().data());
}

static PyObject *LineEdit_selectAll(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    line_edit->selectAll();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *LineEdit_setClearButtonEnabled(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    bool enabled;

    if (!PythonSupport::instance()->parse()(args, "Ob", &obj0, &enabled))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    line_edit->setClearButtonEnabled(enabled);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *LineEdit_setEditable(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    bool editable;

    if (!PythonSupport::instance()->parse()(args, "Ob", &obj0, &editable))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    line_edit->setReadOnly(!editable);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *LineEdit_setPlaceholderText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    line_edit->setPlaceholderText(Py_UNICODE_to_QString(text_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *LineEdit_setText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
        return NULL;

    PyLineEdit *line_edit = Unwrap<PyLineEdit>(obj0);
    if (line_edit == NULL)
        return NULL;

    line_edit->setText(Py_UNICODE_to_QString(text_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListModel_beginInsertRows(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int first_index = -1;
    int last_index = -1;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &first_index, &last_index))
        return NULL;

    // Grab the item controller (a python object)
    ListModel *py_list_model = Unwrap<ListModel>(obj0);
    if (py_list_model == NULL)
        return NULL;

    py_list_model->beginInsertRowsInList(first_index, last_index);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListModel_beginRemoveRows(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int first_index = -1;
    int last_index = -1;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &first_index, &last_index))
        return NULL;

    // Grab the item controller (a python object)
    ListModel *py_list_model = Unwrap<ListModel>(obj0);
    if (py_list_model == NULL)
        return NULL;

    py_list_model->beginRemoveRowsInList(first_index, last_index);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListModel_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the item controller (a python object)
    ListModel *py_list_model = Unwrap<ListModel>(obj0);
    if (py_list_model == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    py_list_model->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListModel_create(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the role names
    QVariant py_role_names = PyObjectToQVariant(obj0);
    QStringList role_names = py_role_names.toStringList();

    ListModel *py_list_model = new ListModel(role_names);

    return WrapQObject(py_list_model);
}

// TODO: Still used?
static PyObject *ListModel_dataChanged(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the list controller (a python object)
    ListModel *py_list_model = Unwrap<ListModel>(obj0);
    if (py_list_model == NULL)
        return NULL;

    py_list_model->dataChangedInList();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListModel_destroy(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the list controller (a python object)
    ListModel *py_list_model = Unwrap<ListModel>(obj0);
    if (py_list_model == NULL)
        return NULL;

    py_list_model->deleteLater();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListModel_endInsertRow(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the item controller (a python object)
    ListModel *py_list_model = Unwrap<ListModel>(obj0);
    if (py_list_model == NULL)
        return NULL;

    py_list_model->endInsertRowsInList();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListModel_endRemoveRow(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the item controller (a python object)
    ListModel *py_list_model = Unwrap<ListModel>(obj0);
    if (py_list_model == NULL)
        return NULL;

    py_list_model->endRemoveRowsInList();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListWidget_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the content
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    ListWidget *py_list_widget = dynamic_cast<ListWidget *>(scroll_area->widget());
    if (py_list_widget == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    py_list_widget->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListWidget_getCurrentRow(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the data view
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    ListWidget *py_list_widget = dynamic_cast<ListWidget *>(scroll_area->widget());
    if (py_list_widget == NULL)
        return NULL;

    return PythonSupport::instance()->build()("i", py_list_widget->currentIndex().row());
}

static PyObject *ListWidget_getRowAtPoint(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int x, y;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &x, &y))
        return NULL;

    // Grab the data view
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    ListWidget *py_list_widget = dynamic_cast<ListWidget *>(scroll_area->widget());
    if (py_list_widget == NULL)
        return NULL;

    QModelIndex index = py_list_widget->indexAt(QPoint(x, y));

    return PythonSupport::instance()->build()("i", index.row());
}

static PyObject *ListWidget_setCurrentRow(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int index = -1;
    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &index))
        return NULL;

    // Grab the data view
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    ListWidget *py_list_widget = dynamic_cast<ListWidget *>(scroll_area->widget());
    if (py_list_widget == NULL)
        return NULL;

    ListModel *py_list_model = dynamic_cast<ListModel *>(py_list_widget->model());
    if (py_list_model == NULL)
        return NULL;

    QModelIndex model_index = py_list_model->index(index, 0);
    py_list_widget->selectionModel()->setCurrentIndex(model_index, QItemSelectionModel::ClearAndSelect);
    py_list_widget->scrollTo(model_index);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListWidget_setItemDelegate(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the content
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    ListWidget *py_list_widget = dynamic_cast<ListWidget *>(scroll_area->widget());
    if (py_list_widget == NULL)
        return NULL;

    PyStyledItemDelegate *delegate = Unwrap<PyStyledItemDelegate>(obj1);
    if (delegate == NULL)
        return NULL;

    py_list_widget->setItemDelegate(delegate);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListWidget_setModel(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the content
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    ListWidget *py_list_widget = dynamic_cast<ListWidget *>(scroll_area->widget());
    if (py_list_widget == NULL)
        return NULL;

    // Grab the list controller (a python object)
    ListModel *py_list_model = Unwrap<ListModel>(obj1);
    if (py_list_model == NULL)
        return NULL;

    py_list_widget->setModelAndConnect(py_list_model);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListWidget_setSelectedIndexes(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the data view
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    ListWidget *py_list_widget = dynamic_cast<ListWidget *>(scroll_area->widget());
    if (py_list_widget == NULL)
        return NULL;

    ListModel *py_list_model = dynamic_cast<ListModel *>(py_list_widget->model());
    if (py_list_model == NULL)
        return NULL;

    QList<QVariant> q_index_list = PyObjectToQVariant(obj1).toList();
    QList<int> index_list;

    Q_FOREACH(const QVariant &q_int, q_index_list)
    {
        index_list.append(q_int.toInt());
    }

    py_list_widget->selectionModel()->reset();
    Q_FOREACH(int index, index_list)
    {
        QModelIndex model_index = py_list_model->index(index, 0);
        py_list_widget->selectionModel()->setCurrentIndex(model_index, QItemSelectionModel::Select);
    }

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ListWidget_setSelectionMode(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    char *selection_mode_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &selection_mode_c))
        return NULL;

    // Grab the data view
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    ListWidget *py_list_widget = dynamic_cast<ListWidget *>(scroll_area->widget());
    if (py_list_widget == NULL)
        return NULL;

    QStringList selection_modes;
    selection_modes << "none" << "single" << "multi_unused" << "extended" << "contiguous";

    if (!selection_modes.contains(selection_mode_c))
        return NULL;

    py_list_widget->setSelectionMode(static_cast<QAbstractItemView::SelectionMode>(selection_modes.indexOf(selection_mode_c)));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_addAction(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    QAction *action = Unwrap<QAction>(obj1);
    if (action == NULL)
        return NULL;

    menu->addAction(action);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_addMenu(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *title_u = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OuO", &obj0, &title_u, &obj1))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    PyMenu *sub_menu = Unwrap<PyMenu>(obj1);
    if (sub_menu == NULL)
        return NULL;

    sub_menu->setTitle(Py_UNICODE_to_QString(title_u));

    menu->addMenu(sub_menu);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_addSeparator(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    menu->addSeparator();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    menu->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_create(PyObject * /*self*/, PyObject *args)
{
    Q_UNUSED(args)

    PyMenu *menu = new PyMenu();

    return WrapQObject(menu);
}

static PyObject *Menu_destroy(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    menu->deleteLater();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_insertAction(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    PyObject *obj2 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OOO", &obj0, &obj1, &obj2))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    QAction *action = Unwrap<QAction>(obj1);
    if (action == NULL)
        return NULL;

    QAction *before_action = Unwrap<QAction>(obj2);
    if (before_action == NULL)
        return NULL;

    menu->insertAction(before_action, action);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_insertSeparator(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    QAction *before_action = Unwrap<QAction>(obj1);
    if (before_action == NULL)
        return NULL;

    menu->insertSeparator(before_action);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_popup(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int gx, gy;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &gx, &gy))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    if (!menu->isEmpty())
        menu->popup(QPoint(gx, gy));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Menu_removeAction(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyMenu *menu = Unwrap<PyMenu>(obj0);
    if (menu == NULL)
        return NULL;

    QAction *action = Unwrap<QAction>(obj1);
    if (action == NULL)
        return NULL;

    menu->removeAction(action);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *MimeData_create(PyObject * /*self*/, PyObject *args)
{
    Q_UNUSED(args)

    QMimeData *mime_data = new QMimeData();

    return WrapQObject(mime_data);
}

static PyObject *MimeData_dataAsString(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *format_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &format_c))
        return NULL;

    QMimeData *mime_data = Unwrap<QMimeData>(obj0);
    if (mime_data == NULL)
        return NULL;

    // Grab the format
    QString format = QString::fromStdString(format_c);

    return PythonSupport::instance()->build()("s", QString::fromUtf8(mime_data->data(format)).toUtf8().data());
}

static PyObject *MimeData_formats(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    QMimeData *mime_data = Unwrap<QMimeData>(obj0);
    if (mime_data == NULL)
        return NULL;

    return PythonSupport::instance()->getPyListFromStrings(mime_data->formats());
}

static PyObject *MimeData_setDataAsString(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *format_c = NULL;
    char *data_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Oss", &obj0, &format_c, &data_c))
        return NULL;

    QMimeData *mime_data = Unwrap<QMimeData>(obj0);
    if (mime_data == NULL)
        return NULL;

    // Grab the format
    QString format = QString::fromStdString(format_c);

    // Grab the data
    QString data = QString::fromStdString(data_c);

    mime_data->setData(format, data.toUtf8().data());

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Output_out(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    Py_UNICODE *output_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &output_u))
        return NULL;

    // Grab the window view
    QTextEdit *text_edit = Unwrap<QTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

    text_edit->moveCursor(QTextCursor::End);
    text_edit->insertPlainText(QString("%1\n").arg(Py_UNICODE_to_QString(output_u).trimmed()));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *PushButton_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyPushButton *push_button = Unwrap<PyPushButton>(obj0);
    if (push_button == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    push_button->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *PushButton_setIcon(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    int width, height;

    if (!PythonSupport::instance()->parse()(args, "OiiO", &obj0, &width, &height, &obj1))
        return NULL;

    PyPushButton *push_button = Unwrap<PyPushButton>(obj0);
    if (push_button == NULL)
        return NULL;

    if (!PythonSupport::instance()->isNone(obj1))
    {
        QImage image = PythonSupport::instance()->imageFromArray(obj1);

        if (image.isNull())
            return NULL;

        push_button->setIcon(QIcon(QPixmap::fromImage(image)));
        push_button->setIconSize(QSize(width, height));
    }
    else
    {
        push_button->setIcon(QIcon());
    }

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *PushButton_setText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
        return NULL;

    PyPushButton *push_button = Unwrap<PyPushButton>(obj0);
    if (push_button == NULL)
        return NULL;

    push_button->setText(Py_UNICODE_to_QString(text_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ScrollArea_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyScrollArea *scroll_area = Unwrap<PyScrollArea>(obj0);
    if (scroll_area == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    scroll_area->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ScrollArea_info(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    QScrollArea *scroll_area = Unwrap<QScrollArea>(obj0);
    if (scroll_area == NULL)
        return NULL;

    qDebug() << "v " << scroll_area->verticalScrollBar()->value() << "/" << scroll_area->verticalScrollBar()->maximum();
    qDebug() << "h " << scroll_area->horizontalScrollBar()->value() << "/" << scroll_area->horizontalScrollBar()->maximum();
    qDebug() << "vp " << scroll_area->viewport()->rect();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ScrollArea_setHorizontal(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    float value = 0.5;
    if (!PythonSupport::instance()->parse()(args, "Of", &obj0, &value))
        return NULL;

    QScrollArea *scroll_area = Unwrap<QScrollArea>(obj0);
    if (scroll_area == NULL)
        return NULL;

    scroll_area->horizontalScrollBar()->setValue(int(scroll_area->horizontalScrollBar()->maximum() * value));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ScrollArea_setScrollbarPolicies(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    char *horizontal_policy_c = NULL;
    char *vertical_policy_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Oss", &obj0, &horizontal_policy_c, &vertical_policy_c))
        return NULL;

    QScrollArea *scroll_area = Unwrap<QScrollArea>(obj0);
    if (scroll_area == NULL)
        return NULL;

    scroll_area->setHorizontalScrollBarPolicy(ParseScrollBarPolicy(horizontal_policy_c));
    scroll_area->setVerticalScrollBarPolicy(ParseScrollBarPolicy(vertical_policy_c));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ScrollArea_setVertical(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    float value = 0.5;
    if (!PythonSupport::instance()->parse()(args, "Of", &obj0, &value))
        return NULL;

    QScrollArea *scroll_area = Unwrap<QScrollArea>(obj0);
    if (scroll_area == NULL)
        return NULL;

    scroll_area->verticalScrollBar()->setValue(int(scroll_area->verticalScrollBar()->maximum() * value));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *ScrollArea_setWidget(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    QScrollArea *scroll_area = Unwrap<QScrollArea>(obj0);
    if (scroll_area == NULL)
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj1);
    if (widget == NULL)
        return NULL;

    scroll_area->setWidget(widget);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Settings_getString(PyObject * /*self*/, PyObject *args)
{
    char *key_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "s", &key_c))
        return NULL;

    // Grab the key
    QString key = QString::fromStdString(key_c);

    QSettings settings;

    QVariant result = settings.value(key);

    if (result.isValid())
        return PythonSupport::instance()->build()("s", result.toString().toUtf8().data());

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Settings_remove(PyObject * /*self*/, PyObject *args)
{
    char *key_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "s", &key_c))
        return NULL;

    // Grab the key
    QString key = QString::fromStdString(key_c);

    QSettings settings;

    settings.remove(key);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Settings_setString(PyObject * /*self*/, PyObject *args)
{
    char *key_c = NULL;
    char *value_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "ss", &key_c, &value_c))
        return NULL;

    // Grab the key
    QString key = QString::fromStdString(key_c);

    // Grab the value
    QString value = QString::fromStdString(value_c);

    QSettings settings;

    settings.setValue(key, value);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Slider_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PySlider *slider = Unwrap<PySlider>(obj0);
    if (slider == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    slider->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Slider_getValue(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PySlider *slider = Unwrap<PySlider>(obj0);
    if (slider == NULL)
        return NULL;

    return PythonSupport::instance()->build()("i", slider->value());
}

static PyObject *Slider_setMaximum(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int value = 0;

    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &value))
        return NULL;

    PySlider *slider = Unwrap<PySlider>(obj0);
    if (slider == NULL)
        return NULL;

    slider->setMaximum(value);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Slider_setMinimum(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int value = 0;

    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &value))
        return NULL;

    PySlider *slider = Unwrap<PySlider>(obj0);
    if (slider == NULL)
        return NULL;

    slider->setMinimum(value);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Slider_setValue(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int value = 0;

    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &value))
        return NULL;

    PySlider *slider = Unwrap<PySlider>(obj0);
    if (slider == NULL)
        return NULL;

    slider->setValue(value);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Splitter_restoreState(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *settings_id_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &settings_id_c))
        return NULL;

    // Grab the splitter
    QSplitter *splitter = Unwrap<QSplitter>(obj0);
    if (splitter == NULL)
        return NULL;

    QSettings settings;
    splitter->restoreState(settings.value(settings_id_c).toByteArray());

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Splitter_saveState(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *settings_id_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &settings_id_c))
        return NULL;

    // Grab the splitter
    QSplitter *splitter = Unwrap<QSplitter>(obj0);
    if (splitter == NULL)
        return NULL;

    QSettings settings;
    settings.setValue(settings_id_c, splitter->saveState());

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Splitter_setOrientation(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *orientation_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &orientation_c))
        return NULL;

    // Grab the splitter
    QSplitter *splitter = Unwrap<QSplitter>(obj0);
    if (splitter == NULL)
        return NULL;

    Qt::Orientation orientation = Qt::Vertical;
    if (strcmp(orientation_c, "horizontal") == 0)
        orientation = Qt::Horizontal;

    splitter->setOrientation(orientation);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *StackWidget_addWidget(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the container
    QStackedWidget *container = Unwrap<QStackedWidget>(obj0);
    if (container == NULL)
        return NULL;

    // Grab the widget to add
    QWidget *widget = Unwrap<QWidget>(obj1);
    if (widget == NULL)
        return NULL;

    return PythonSupport::instance()->build()("i", container->addWidget(widget));
}

static PyObject *StackWidget_removeWidget(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the container
    QStackedWidget *container = Unwrap<QStackedWidget>(obj0);
    if (container == NULL)
        return NULL;

    // Grab the widget to add
    QWidget *widget = Unwrap<QWidget>(obj1);
    if (widget == NULL)
        return NULL;

    container->removeWidget(widget);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *StackWidget_setCurrentIndex(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    int index = -1;
    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &index))
        return NULL;

    // Grab the container
    QStackedWidget *container = Unwrap<QStackedWidget>(obj0);
    if (container == NULL)
        return NULL;

    container->setCurrentIndex(index);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *StyledDelegate_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyStyledItemDelegate *delegate = Unwrap<PyStyledItemDelegate>(obj0);
    if (delegate == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    delegate->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *StyledDelegate_create(PyObject * /*self*/, PyObject *args)
{
    Q_UNUSED(args)

    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyStyledItemDelegate *delegate = new PyStyledItemDelegate();

    return WrapQObject(delegate);
}

static PyObject *TabWidget_addTab(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    Py_UNICODE *label_u = NULL;
    if (!PythonSupport::instance()->parse()(args, "OOu", &obj0, &obj1, &label_u))
        return NULL;

    // Grab the container (tab widget)
    PyTabWidget *container = Unwrap<PyTabWidget>(obj0);
    if (container == NULL)
        return NULL;

    // Grab the widget to add
    QWidget *widget = Unwrap<QWidget>(obj1);
    if (widget == NULL)
        return NULL;

    container->addTab(widget, Py_UNICODE_to_QString(label_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TabWidget_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyTabWidget *text_edit = Unwrap<PyTabWidget>(obj0);
    if (text_edit == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    text_edit->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TextEdit_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;

    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    PyTextEdit *text_edit = Unwrap<PyTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    text_edit->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TextEdit_getEditable(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyTextEdit *text_edit = Unwrap<PyTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

    return PythonSupport::instance()->build()("b", !text_edit->isReadOnly());
}

static PyObject *TextEdit_getPlaceholderText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyTextEdit *text_edit = Unwrap<PyTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

#if !defined(Q_OS_LINUX)
    return PythonSupport::instance()->build()("s", text_edit->placeholderText().toUtf8().data());
#else
    return PythonSupport::instance()->build()("s", QString().toUtf8().data());
#endif
}

static PyObject *TextEdit_getText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyTextEdit *text_edit = Unwrap<PyTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

    return PythonSupport::instance()->build()("s", text_edit->toPlainText().toUtf8().data());
}

static PyObject *TextEdit_selectAll(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    PyTextEdit *text_edit = Unwrap<PyTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

    text_edit->selectAll();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TextEdit_setEditable(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    bool editable;

    if (!PythonSupport::instance()->parse()(args, "Ob", &obj0, &editable))
        return NULL;

    PyTextEdit *text_edit = Unwrap<PyTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

    text_edit->setReadOnly(!editable);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TextEdit_setPlaceholderText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
        return NULL;

    PyTextEdit *text_edit = Unwrap<PyTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

#if !defined(Q_OS_LINUX)
    text_edit->setPlaceholderText(Py_UNICODE_to_QString(text_u));
#endif

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TextEdit_setText(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    Py_UNICODE *text_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &text_u))
        return NULL;

    PyTextEdit *text_edit = Unwrap<PyTextEdit>(obj0);
    if (text_edit == NULL)
        return NULL;

    text_edit->setText(Py_UNICODE_to_QString(text_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TreeWidget_connect(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the content
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    TreeWidget *py_tree_widget = dynamic_cast<TreeWidget *>(scroll_area->widget());
    if (py_tree_widget == NULL)
        return NULL;

    QVariant py_object = PyObjectToQVariant(obj1);

    py_tree_widget->setPyObject(py_object);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TreeWidget_setCurrentRow(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int index = -1;
    int parent_row = -1;
    int parent_item_id = -1;
    if (!PythonSupport::instance()->parse()(args, "Oiii", &obj0, &index, &parent_row, &parent_item_id))
        return NULL;

    // Grab the data view
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    TreeWidget *py_tree_widget = dynamic_cast<TreeWidget *>(scroll_area->widget());
    if (py_tree_widget == NULL)
        return NULL;

    ItemModel *py_item_model = dynamic_cast<ItemModel *>(py_tree_widget->model());
    if (py_item_model == NULL)
        return NULL;

    QModelIndex model_index = py_item_model->indexInParent(index, parent_row, parent_item_id);
    py_tree_widget->setCurrentIndex(model_index);
    py_tree_widget->scrollTo(model_index);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TreeWidget_setItemDelegate(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the content
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    TreeWidget *py_tree_widget = dynamic_cast<TreeWidget *>(scroll_area->widget());
    if (py_tree_widget == NULL)
        return NULL;

    PyStyledItemDelegate *delegate = Unwrap<PyStyledItemDelegate>(obj1);
    if (delegate == NULL)
        return NULL;

    py_tree_widget->setItemDelegate(delegate);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TreeWidget_setModel(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the content
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    TreeWidget *py_tree_widget = dynamic_cast<TreeWidget *>(scroll_area->widget());
    if (py_tree_widget == NULL)
        return NULL;

    // Grab the list controller (a python object)
    ItemModel *py_item_model = Unwrap<ItemModel>(obj1);
    if (py_item_model == NULL)
        return NULL;

    py_tree_widget->setModelAndConnect(py_item_model);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *TreeWidget_setSelectionMode(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    char *selection_mode_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &selection_mode_c))
        return NULL;

    // Grab the content
    QWidget *content_view = Unwrap<QWidget>(obj0);
    if (content_view == NULL)
        return NULL;
    QScrollArea *scroll_area = dynamic_cast<QScrollArea *>(content_view->layout()->itemAt(0)->widget());
    if (scroll_area == NULL)
        return NULL;
    TreeWidget *py_tree_widget = dynamic_cast<TreeWidget *>(scroll_area->widget());
    if (py_tree_widget == NULL)
        return NULL;

    QStringList selection_modes;
    selection_modes << "none" << "single" << "multi_unused" << "extended" << "contiguous";

    if (!selection_modes.contains(selection_mode_c))
    return NULL;

    py_tree_widget->setSelectionMode(static_cast<QAbstractItemView::SelectionMode>(selection_modes.indexOf(selection_mode_c)));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_addOverlay(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the widget
    QWidget *parent_widget = Unwrap<QWidget>(obj0);
    if (parent_widget == NULL)
        return NULL;

    // Grab the widget
    QWidget *child_widget = Unwrap<QWidget>(obj1);
    if (child_widget == NULL)
        return NULL;

    new Overlay(parent_widget, child_widget);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_addSpacing(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int spacing = 0;
    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &spacing))
    return NULL;

    // Grab the container
    QWidget *container = Unwrap<QWidget>(obj0);
    if (container == NULL)
    return NULL;

    QBoxLayout *layout = dynamic_cast<QBoxLayout *>(container->layout());
    layout->addSpacing(spacing);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_addStretch(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the container
    QWidget *container = Unwrap<QWidget>(obj0);
    if (container == NULL)
        return NULL;

    QBoxLayout *layout = dynamic_cast<QBoxLayout *>(container->layout());
    layout->addStretch();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_addWidget(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OO", &obj0, &obj1))
        return NULL;

    // Grab the container
    QWidget *container = Unwrap<QWidget>(obj0);
    if (container == NULL)
        return NULL;

    // Grab the widget to add
    QWidget *widget = Unwrap<QWidget>(obj1);
    if (widget == NULL)
        return NULL;

    // hack for splitter
    if (dynamic_cast<QSplitter *>(container) != NULL)
    {
        dynamic_cast<QSplitter *>(container)->addWidget(widget);
    }
    else
    {
        container->layout()->addWidget(widget);
        // now force the layout to re-layout
        container->layout()->setGeometry(container->layout()->geometry());
    }

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_adjustSize(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the container
    QWidget *container = Unwrap<QWidget>(obj0);
    if (container == NULL)
        return NULL;

    // force size to adjust
    container->adjustSize();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_clearFocus(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    widget->clearFocus();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_hide(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the widget to add
    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    widget->hide();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_getWidgetProperty(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *property_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &property_c))
        return NULL;

    // Grab the widget
    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    QVariant result = Widget_getWidgetProperty_(widget, QString::fromStdString(property_c));

    return QVariantToPyObject(result);
}

static PyObject *Widget_grabGesture(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *gesture_type_c = NULL;

    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &gesture_type_c))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    QStringList gesture_types;
    gesture_types << "tap" << "long-tap" << "pan" << "pinch" << "swipe";

    if (!gesture_types.contains(gesture_type_c))
        return NULL;

    widget->grabGesture(static_cast<Qt::GestureType>(gesture_types.indexOf(gesture_type_c) + 1));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_hasFocus(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    return PythonSupport::instance()->build()("b", widget->hasFocus());
}

static PyObject *Widget_insertWidget(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    PyObject *obj1 = NULL;
    int index = -1;
    bool fill = 0;
    char *alignment_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "OOibz", &obj0, &obj1, &index, &fill, &alignment_c))
        return NULL;

    // Grab the container
    QWidget *container = Unwrap<QWidget>(obj0);
    if (container == NULL)
        return NULL;

    // Grab the widget to add
    QWidget *widget = Unwrap<QWidget>(obj1);
    if (widget == NULL)
        return NULL;

    // Alignment
    Qt::Alignment alignment = 0;
    if (alignment_c)
    {
        if (strcmp(alignment_c, "left") == 0)
            alignment = Qt::AlignLeft;
        else if (strcmp(alignment_c, "right") == 0)
            alignment = Qt::AlignRight;
        else if (strcmp(alignment_c, "hcenter") == 0)
            alignment = Qt::AlignHCenter;
        else if (strcmp(alignment_c, "justify") == 0)
            alignment = Qt::AlignJustify;
        else if (strcmp(alignment_c, "top") == 0)
            alignment = Qt::AlignTop;
        else if (strcmp(alignment_c, "bottom") == 0)
            alignment = Qt::AlignBottom;
        else if (strcmp(alignment_c, "vcenter") == 0)
            alignment = Qt::AlignVCenter;
        else if (strcmp(alignment_c, "center") == 0)
            alignment = Qt::AlignCenter;
    }

    // Stretch hardcoded to 0
    int stretch = 0;

    QBoxLayout *box_layout = dynamic_cast<QBoxLayout *>(container->layout());
    if (!box_layout)
        return NULL;
    box_layout->insertWidget(index, widget, stretch, alignment);

    if (fill)
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // now force the layout to re-layout
    container->layout()->setGeometry(container->layout()->geometry());

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_isEnabled(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    return PythonSupport::instance()->build()("b", widget->isEnabled());
}

static PyObject *Widget_isVisible(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;

    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    return PythonSupport::instance()->build()("b", widget->isVisible());
}

static PyObject *Widget_loadIntrinsicWidget(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    char *intrinsic_id_c = NULL;
    if (!PythonSupport::instance()->parse()(args, "s", &intrinsic_id_c))
        return NULL;

    QWidget *widget = Widget_makeIntrinsicWidget(QString::fromStdString(intrinsic_id_c));

    return WrapQObject(widget);
}

static PyObject *Widget_mapToGlobal(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    int x, y;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &x, &y))
        return NULL;

    // Grab the widget
    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    QPoint p = widget->mapToGlobal(QPoint(x, y));

    return PythonSupport::instance()->build()("ii", p.x(), p.y());
}

static PyObject *Widget_removeAll(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the widget
    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    while (widget->layout()->count() > 0)
        delete widget->layout()->takeAt(0);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_removeWidget(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the widget
    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    delete widget;

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_setEnabled(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    bool enabled;

    if (!PythonSupport::instance()->parse()(args, "Ob", &obj0, &enabled))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    widget->setEnabled(enabled);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_setFocus(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    int reason;

    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &reason))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    widget->setFocus((Qt::FocusReason)reason);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_setToolTip(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    Py_UNICODE *tool_tip_u = NULL;

    if (!PythonSupport::instance()->parse()(args, "Ou", &obj0, &tool_tip_u))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    widget->setToolTip(Py_UNICODE_to_QString(tool_tip_u));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_setVisible(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    bool visible;

    if (!PythonSupport::instance()->parse()(args, "Ob", &obj0, &visible))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    widget->setVisible(visible);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_setWidgetProperty(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    char *property_c = NULL;
    PyObject *obj2 = NULL;
    if (!PythonSupport::instance()->parse()(args, "OsO", &obj0, &property_c, &obj2))
        return NULL;

    // Grab the widget
    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    // Grab the value (a python object)
    QVariant py_value = PyObjectToQVariant(obj2);

    Widget_setWidgetProperty_(widget, QString::fromStdString(property_c), py_value);

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_setWidgetSize(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    int width = 0, height = 0;
    if (!PythonSupport::instance()->parse()(args, "Oii", &obj0, &width, &height))
        return NULL;

    // Grab the container
    QWidget *container = Unwrap<QWidget>(obj0);
    if (container == NULL)
        return NULL;

    // force size to adjust
    container->resize(QSize(width, height));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_show(PyObject * /*self*/, PyObject *args)
{
    if (qApp->thread() != QThread::currentThread())
    {
        PythonSupport::instance()->setErrorString("Must be called on UI thread.");
        return NULL;
    }

    PyObject *obj0 = NULL;
    if (!PythonSupport::instance()->parse()(args, "O", &obj0))
        return NULL;

    // Grab the widget to add
    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    widget->show();

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_ungrabGesture(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    char *gesture_type_c = NULL;

    if (!PythonSupport::instance()->parse()(args, "Os", &obj0, &gesture_type_c))
        return NULL;

    QWidget *widget = Unwrap<QWidget>(obj0);
    if (widget == NULL)
        return NULL;

    QStringList gesture_types;
    gesture_types << "tap" << "long-tap" << "pan" << "pinch" << "swipe";

    if (!gesture_types.contains(gesture_type_c))
        return NULL;

    widget->ungrabGesture(static_cast<Qt::GestureType>(gesture_types.indexOf(gesture_type_c) + 1));

    return PythonSupport::instance()->getNoneReturnValue();
}

static PyObject *Widget_widgetByIndex(PyObject * /*self*/, PyObject *args)
{
    PyObject *obj0 = NULL;
    int index = -1;
    if (!PythonSupport::instance()->parse()(args, "Oi", &obj0, &index))
        return NULL;

    // Grab the container
    QWidget *container = Unwrap<QWidget>(obj0);
    if (container == NULL)
        return NULL;

    QWidget *widget = container->layout()->itemAt(index)->widget();

    return WrapQObject(widget);
}

#endif // !USE_THRIFT

//----

void Application::output(const QString &str)
{
    QString str_ = str.trimmed();
    if (!str_.isEmpty())
        qDebug() << str.trimmed().toStdString().c_str();
}

Application::Application(int & argv, char **args)
    : QApplication(argv, args)
    , m_quit_on_last_window(false)
    , m_idle_enabled(false)
{
    setQuitOnLastWindowClosed(true);

    m_idle_timer = new QTimer(this);
    connect(m_idle_timer, SIGNAL(timeout()), this, SLOT(idle()));
    m_idle_timer->start(1000.0/20);

    connect(this, SIGNAL(lastWindowClosed()), this, SLOT(continueQuit()));

    connect(this, SIGNAL(aboutToQuit()), this, SLOT(aboutToQuit()));

    // these constaints are defined in LauncherConfig.h
    setApplicationName(APP_NAME);
    setOrganizationName(ORGANIZATION_NAME);
    setOrganizationDomain(ORGANIZATION_DOMAIN);

    // TODO: Handle case where python home contains no dylib/dll.
    // TODO: Handle case where python home contains wrong version of python.
    // TODO: Handle case where python home missing required packages.
    // TODO: Handle case where python home exists.

    m_python_home = argv > 1 ? QString::fromUtf8(args[1]) : QString();
    m_python_target = argv > 2 ? QString::fromUtf8(args[2]) : QString();
}

Application::~Application()
{
    m_idle_timer->stop();
}

#if !USE_THRIFT

static PyMethodDef Methods[] = {
    {"Action_connect", Action_connect, METH_VARARGS, "Action_connect."},
    {"Action_create", Action_create, METH_VARARGS, "Action_create."},
    {"Action_setChecked", Action_setChecked, METH_VARARGS, "Action_setChecked."},
    {"Action_setEnabled", Action_setEnabled, METH_VARARGS, "Action_setEnabled."},
    {"Action_setTitle", Action_setTitle, METH_VARARGS, "Action_setTitle."},
    {"Application_close", Application_close, METH_VARARGS, "Application_close."},
    {"Canvas_connect", Canvas_connect, METH_VARARGS, "Canvas_connect."},
    {"Canvas_draw", Canvas_draw, METH_VARARGS, "Canvas_draw."},
    {"Canvas_grabMouse", Canvas_grabMouse, METH_VARARGS, "Canvas_grabMouse."},
    {"Canvas_releaseMouse", Canvas_releaseMouse, METH_VARARGS, "Canvas_releaseMouse."},
    {"Canvas_setCursorShape", Canvas_setCursorShape, METH_VARARGS, "Canvas_setCursorShape."},
    {"Canvas_setFocusPolicy", Canvas_setFocusPolicy, METH_VARARGS, "Canvas_setFocusPolicy."},
    {"CheckBox_connect", CheckBox_connect, METH_VARARGS, "CheckBox_connect."},
    {"CheckBox_getCheckState", CheckBox_getCheckState, METH_VARARGS, "CheckBox_getCheckState."},
    {"CheckBox_getIsTristate", CheckBox_getIsTristate, METH_VARARGS, "CheckBox_getIsTristate."},
    {"CheckBox_setCheckState", CheckBox_setCheckState, METH_VARARGS, "CheckBox_setCheckState."},
    {"Clipboard_clear", Clipboard_clear, METH_VARARGS, "Clipboard_clear."},
    {"Clipboard_mimeData", Clipboard_mimeData, METH_VARARGS, "Clipboard_mimeData."},
    {"Clipboard_setMimeData", Clipboard_setMimeData, METH_VARARGS, "Clipboard_setMimeData."},
    {"Clipboard_setText", Clipboard_setText, METH_VARARGS, "Clipboard_setText."},
    {"Clipboard_text", Clipboard_text, METH_VARARGS, "Clipboard_text."},
    {"CheckBox_setText", CheckBox_setText, METH_VARARGS, "CheckBox_setText."},
    {"CheckBox_setIsTristate", CheckBox_setIsTristate, METH_VARARGS, "CheckBox_setIsTristate."},
    {"ComboBox_addItem", ComboBox_addItem, METH_VARARGS, "ComboBox_addItem."},
    {"ComboBox_connect", ComboBox_connect, METH_VARARGS, "ComboBox_connect."},
    {"ComboBox_getCurrentText", ComboBox_getCurrentText, METH_VARARGS, "ComboBox_getCurrentText."},
    {"ComboBox_removeAllItems", ComboBox_removeAllItems, METH_VARARGS, "ComboBox_removeAllItems."},
    {"ComboBox_setCurrentText", ComboBox_setCurrentText, METH_VARARGS, "ComboBox_setCurrentText."},
    {"Console_connect", Console_connect, METH_VARARGS, "Console_connect."},
    {"Console_insertFromStringList", Console_insertFromStringList, METH_VARARGS, "Console_insertFromStringList."},
    {"Core_getFontMetrics", Core_getFontMetrics, METH_VARARGS, "Core_getFontMetrics."},
    {"Core_getLocation", Core_getLocation, METH_VARARGS, "Core_getLocation."},
    {"Core_out", Core_out, METH_VARARGS, "Core_out."},
    {"Core_pathToURL", Core_pathToURL, METH_VARARGS, "Core_pathToURL."},
    {"Core_readImageToBinary", Core_readImageToBinary, METH_VARARGS, "Core_readImageToBinary."},
    {"Core_URLToPath", Core_URLToPath, METH_VARARGS, "Core_URLToPath."},
    {"Core_writeBinaryToImage", Core_writeBinaryToImage, METH_VARARGS, "Core_writeBinaryToImage."},
    {"DockWidget_getToggleAction", DockWidget_getToggleAction, METH_VARARGS, "DockWidget_getToggleAction."},
    {"DocumentWindow_addDockWidget", DocumentWindow_addDockWidget, METH_VARARGS, "DocumentWindow_addDockWidget."},
    {"DocumentWindow_addMenu", DocumentWindow_addMenu, METH_VARARGS, "DocumentWindow_addMenu."},
    {"DocumentWindow_close", DocumentWindow_close, METH_VARARGS, "DocumentWindow_close."},
    {"DocumentWindow_connect", DocumentWindow_connect, METH_VARARGS, "DocumentWindow_connect."},
    {"DocumentWindow_create", DocumentWindow_create, METH_VARARGS, "DocumentWindow_create."},
    {"DocumentWindow_getFilePath", DocumentWindow_getFilePath, METH_VARARGS, "DocumentWindow_getFilePath."},
    {"DocumentWindow_insertMenu", DocumentWindow_insertMenu, METH_VARARGS, "DocumentWindow_insertMenu."},
    {"DocumentWindow_removeDockWidget", DocumentWindow_removeDockWidget, METH_VARARGS, "DocumentWindow_removeDockWidget."},
    {"DocumentWindow_restore", DocumentWindow_restore, METH_VARARGS, "DocumentWindow_restore."},
    {"DocumentWindow_setCentralWidget", DocumentWindow_setCentralWidget, METH_VARARGS, "DocumentWindow_setCentralWidget."},
    {"DocumentWindow_setPosition", DocumentWindow_setPosition, METH_VARARGS, "DocumentWindow_setPosition."},
    {"DocumentWindow_setSize", DocumentWindow_setSize, METH_VARARGS, "DocumentWindow_setSize."},
    {"DocumentWindow_setTitle", DocumentWindow_setTitle, METH_VARARGS, "DocumentWindow_setTitle."},
    {"DocumentWindow_show", DocumentWindow_show, METH_VARARGS, "DocumentWindow_show."},
    {"DocumentWindow_tabifyDockWidgets", DocumentWindow_tabifyDockWidgets, METH_VARARGS, "DocumentWindow_tabifyDockWidgets."},
    {"Drag_connect", Drag_connect, METH_VARARGS, "Drag_connect."},
    {"Drag_create", Drag_create, METH_VARARGS, "Drag_create."},
    {"Drag_exec", Drag_exec, METH_VARARGS, "Drag_exec."},
    {"Drag_setThumbnail", Drag_setThumbnail, METH_VARARGS, "Drag_setThumbnail."},
    {"DrawingContext_drawCommands", DrawingContext_drawCommands, METH_VARARGS, "DrawingContext_drawCommands."},
    {"DrawingContext_paintRGBA", DrawingContext_paintRGBA, METH_VARARGS, "DrawingContext_paintRGBA."},
    {"ItemModel_beginInsertRows", ItemModel_beginInsertRows, METH_VARARGS, "ItemModel beginInsertRows."},
    {"ItemModel_beginRemoveRows", ItemModel_beginRemoveRows, METH_VARARGS, "ItemModel beginRemoveRows."},
    {"ItemModel_connect", ItemModel_connect, METH_VARARGS, "ItemModel_connect."},
    {"ItemModel_create", ItemModel_create, METH_VARARGS, "ItemModel create."},
    {"ItemModel_dataChanged", ItemModel_dataChanged, METH_VARARGS, "ItemModel_dataChanged."},
    {"ItemModel_destroy", ItemModel_destroy, METH_VARARGS, "ItemModel destroy."},
    {"ItemModel_endInsertRow", ItemModel_endInsertRow, METH_VARARGS, "ItemModel endInsertRows."},
    {"ItemModel_endRemoveRow", ItemModel_endRemoveRow, METH_VARARGS, "ItemModel endRemoveRows."},
    {"Label_setText", Label_setText, METH_VARARGS, "Label_setText."},
    {"LineEdit_connect", LineEdit_connect, METH_VARARGS, "LineEdit_connect."},
    {"LineEdit_getEditable", LineEdit_getEditable, METH_VARARGS, "LineEdit_getEditable."},
    {"LineEdit_getPlaceholderText", LineEdit_getPlaceholderText, METH_VARARGS, "LineEdit_getPlaceholderText."},
    {"LineEdit_getText", LineEdit_getText, METH_VARARGS, "LineEdit_getText."},
    {"LineEdit_selectAll", LineEdit_selectAll, METH_VARARGS, "LineEdit_selectAll."},
    {"LineEdit_setClearButtonEnabled", LineEdit_setClearButtonEnabled, METH_VARARGS, "LineEdit_setClearButtonEnabled."},
    {"LineEdit_setEditable", LineEdit_setEditable, METH_VARARGS, "LineEdit_setEditable."},
    {"LineEdit_setPlaceholderText", LineEdit_setPlaceholderText, METH_VARARGS, "LineEdit_setPlaceholderText."},
    {"LineEdit_setText", LineEdit_setText, METH_VARARGS, "LineEdit_setText."},
    {"ListModel_beginInsertRows", ListModel_beginInsertRows, METH_VARARGS, "ListModel beginInsertRows."},
    {"ListModel_beginRemoveRows", ListModel_beginRemoveRows, METH_VARARGS, "ListModel beginRemoveRows."},
    {"ListModel_connect", ListModel_connect, METH_VARARGS, "ListModel_connect."},
    {"ListModel_create", ListModel_create, METH_VARARGS, "ListModel create."},
    {"ListModel_dataChanged", ListModel_dataChanged, METH_VARARGS, "ListModel dataChanged."},
    {"ListModel_destroy", ListModel_destroy, METH_VARARGS, "ListModel destroy."},
    {"ListModel_endInsertRow", ListModel_endInsertRow, METH_VARARGS, "ListModel endInsertRows."},
    {"ListModel_endRemoveRow", ListModel_endRemoveRow, METH_VARARGS, "ListModel endRemoveRows."},
    {"ListWidget_connect", ListWidget_connect, METH_VARARGS, "ListWidget_connect."},
    {"ListWidget_getCurrentRow", ListWidget_getCurrentRow, METH_VARARGS, "ListWidget_getCurrentRow."},
    {"ListWidget_getRowAtPoint", ListWidget_getRowAtPoint, METH_VARARGS, "ListWidget_getRowAtPoint."},
    {"ListWidget_setCurrentRow", ListWidget_setCurrentRow, METH_VARARGS, "ListWidget_setCurrentRow."},
    {"ListWidget_setItemDelegate", ListWidget_setItemDelegate, METH_VARARGS, "ListWidget_setItemDelegate."},
    {"ListWidget_setModel", ListWidget_setModel, METH_VARARGS, "ListWidget_setModel."},
    {"ListWidget_setSelectedIndexes", ListWidget_setSelectedIndexes, METH_VARARGS, "ListWidget_setSelectedIndexes."},
    {"ListWidget_setSelectionMode", ListWidget_setSelectionMode, METH_VARARGS, "ListWidget_setSelectionMode."},
    {"Menu_addAction", Menu_addAction, METH_VARARGS, "Menu_addAction."},
    {"Menu_addMenu", Menu_addMenu, METH_VARARGS, "Menu_addMenu."},
    {"Menu_addSeparator", Menu_addSeparator, METH_VARARGS, "Menu_addSeparator."},
    {"Menu_connect", Menu_connect, METH_VARARGS, "Menu_connect."},
    {"Menu_create", Menu_create, METH_VARARGS, "Menu_create."},
    {"Menu_destroy", Menu_destroy, METH_VARARGS, "Menu_destroy."},
    {"Menu_insertAction", Menu_insertAction, METH_VARARGS, "Menu_insertAction."},
    {"Menu_insertSeparator", Menu_insertSeparator, METH_VARARGS, "Menu_insertSeparator."},
    {"Menu_popup", Menu_popup, METH_VARARGS, "Menu_popup."},
    {"Menu_removeAction", Menu_removeAction, METH_VARARGS, "Menu_removeAction."},
    {"MimeData_create", MimeData_create, METH_VARARGS, "MimeData_create."},
    {"MimeData_dataAsString", MimeData_dataAsString, METH_VARARGS, "MimeData_dataAsString."},
    {"MimeData_formats", MimeData_formats, METH_VARARGS, "MimeData_formats."},
    {"MimeData_setDataAsString", MimeData_setDataAsString, METH_VARARGS, "MimeData_setDataAsString."},
    {"Output_out", Output_out, METH_VARARGS, "Output_out."},
    {"PushButton_connect", PushButton_connect, METH_VARARGS, "PushButton_connect."},
    {"PushButton_setIcon", PushButton_setIcon, METH_VARARGS, "PushButton_setIcon."},
    {"PushButton_setText", PushButton_setText, METH_VARARGS, "PushButton_setText."},
    {"ScrollArea_connect", ScrollArea_connect, METH_VARARGS, "ScrollArea_connect."},
    {"ScrollArea_info", ScrollArea_info, METH_VARARGS, "ScrollArea_info."},
    {"ScrollArea_setHorizontal", ScrollArea_setHorizontal, METH_VARARGS, "ScrollArea_setHorizontal."},
    {"ScrollArea_setScrollbarPolicies", ScrollArea_setScrollbarPolicies, METH_VARARGS, "ScrollArea_setScrollbarPolicies."},
    {"ScrollArea_setVertical", ScrollArea_setVertical, METH_VARARGS, "ScrollArea_setVertical."},
    {"ScrollArea_setWidget", ScrollArea_setWidget, METH_VARARGS, "ScrollArea_setWidget."},
    {"Settings_getString", Settings_getString, METH_VARARGS, "Settings_getString."},
    {"Settings_remove", Settings_remove, METH_VARARGS, "Settings_remove."},
    {"Settings_setString", Settings_setString, METH_VARARGS, "Settings_setString."},
    {"Slider_connect", Slider_connect, METH_VARARGS, "Slider_connect."},
    {"Slider_getValue", Slider_getValue, METH_VARARGS, "Slider_getValue."},
    {"Slider_setMaximum", Slider_setMaximum, METH_VARARGS, "Slider_setMaximum."},
    {"Slider_setMinimum", Slider_setMinimum, METH_VARARGS, "Slider_setMinimum."},
    {"Slider_setValue", Slider_setValue, METH_VARARGS, "Slider_setValue."},
    {"Splitter_restoreState", Splitter_restoreState, METH_VARARGS, "Splitter_restoreState"},
    {"Splitter_saveState", Splitter_saveState, METH_VARARGS, "Splitter_saveState"},
    {"Splitter_setOrientation", Splitter_setOrientation, METH_VARARGS, "Splitter_setOrientation"},
    {"StackWidget_addWidget", StackWidget_addWidget, METH_VARARGS, "StackWidget_addWidget"},
    {"StackWidget_removeWidget", StackWidget_removeWidget, METH_VARARGS, "StackWidget_removeWidget"},
    {"StackWidget_setCurrentIndex", StackWidget_setCurrentIndex, METH_VARARGS, "StackWidget_setCurrentIndex"},
    {"StyledDelegate_connect", StyledDelegate_connect, METH_VARARGS, "StyledDelegate_connect."},
    {"StyledDelegate_create", StyledDelegate_create, METH_VARARGS, "StyledDelegate_create."},
    {"TabWidget_addTab", TabWidget_addTab, METH_VARARGS, "TabWidget_addTab."},
    {"TabWidget_connect", TabWidget_connect, METH_VARARGS, "TabWidget_connect."},
    {"TextEdit_connect", TextEdit_connect, METH_VARARGS, "TextEdit_connect."},
    {"TextEdit_getEditable", TextEdit_getEditable, METH_VARARGS, "TextEdit_getEditable."},
    {"TextEdit_getPlaceholderText", TextEdit_getPlaceholderText, METH_VARARGS, "TextEdit_getPlaceholderText."},
    {"TextEdit_getText", TextEdit_getText, METH_VARARGS, "TextEdit_getText."},
    {"TextEdit_selectAll", TextEdit_selectAll, METH_VARARGS, "TextEdit_selectAll."},
    {"TextEdit_setEditable", TextEdit_setEditable, METH_VARARGS, "TextEdit_setEditable."},
    {"TextEdit_setPlaceholderText", TextEdit_setPlaceholderText, METH_VARARGS, "TextEdit_setPlaceholderText."},
    {"TextEdit_setText", TextEdit_setText, METH_VARARGS, "TextEdit_setText."},
    {"TreeWidget_connect", TreeWidget_connect, METH_VARARGS, "TreeWidget_connect."},
    {"TreeWidget_setCurrentRow", TreeWidget_setCurrentRow, METH_VARARGS, "TreeWidget_setCurrentRow."},
    {"TreeWidget_setItemDelegate", TreeWidget_setItemDelegate, METH_VARARGS, "TreeWidget_setItemDelegate."},
    {"TreeWidget_setModel", TreeWidget_setModel, METH_VARARGS, "TreeWidget_setModel."},
    {"TreeWidget_setSelectionMode", TreeWidget_setSelectionMode, METH_VARARGS, "TreeWidget_setSelectionMode."},
    {"Widget_addOverlay", Widget_addOverlay, METH_VARARGS, "Widget_addOverlay."},
    {"Widget_addSpacing", Widget_addSpacing, METH_VARARGS, "Widget_addSpacing."},
    {"Widget_addStretch", Widget_addStretch, METH_VARARGS, "Widget_addStretch."},
    {"Widget_addWidget", Widget_addWidget, METH_VARARGS, "Widget_addWidget."},
    {"Widget_adjustSize", Widget_adjustSize, METH_VARARGS, "Widget_adjustSize."},
    {"Widget_clearFocus", Widget_clearFocus, METH_VARARGS, "Widget_clearFocus."},
    {"Widget_getWidgetProperty", Widget_getWidgetProperty, METH_VARARGS, "Widget_getWidgetProperty."},
    {"Widget_grabGesture", Widget_grabGesture, METH_VARARGS, "Widget_grabGesture."},
    {"Widget_hasFocus", Widget_hasFocus, METH_VARARGS, "Widget_hasFocus."},
    {"Widget_hide", Widget_hide, METH_VARARGS, "Widget_hide."},
    {"Widget_insertWidget", Widget_insertWidget, METH_VARARGS, "Widget_insertWidget."},
    {"Widget_isEnabled", Widget_isEnabled, METH_VARARGS, "Widget_isEnabled."},
    {"Widget_isVisible", Widget_isVisible, METH_VARARGS, "Widget_isVisible."},
    {"Widget_loadIntrinsicWidget", Widget_loadIntrinsicWidget, METH_VARARGS, "Widget_loadIntrinsicWidget."},
    {"Widget_mapToGlobal", Widget_mapToGlobal, METH_VARARGS, "Widget_mapToGlobal."},
    {"Widget_removeAll", Widget_removeAll, METH_VARARGS, "Widget_removeAll."},
    {"Widget_removeWidget", Widget_removeWidget, METH_VARARGS, "Widget_removeWidget."},
    {"Widget_setEnabled", Widget_setEnabled, METH_VARARGS, "Widget_setEnabled."},
    {"Widget_setFocus", Widget_setFocus, METH_VARARGS, "Widget_setFocus."},
    {"Widget_setToolTip", Widget_setToolTip, METH_VARARGS, "Widget_setToolTip."},
    {"Widget_setVisible", Widget_setVisible, METH_VARARGS, "Widget_setVisible."},
    {"Widget_setWidgetProperty", Widget_setWidgetProperty, METH_VARARGS, "Widget_setWidgetProperty."},
    {"Widget_setWidgetSize", Widget_setWidgetSize, METH_VARARGS, "Widget_setWidgetSize."},
    {"Widget_show", Widget_show, METH_VARARGS, "Widget_show."},
    {"Widget_ungrabGesture", Widget_ungrabGesture, METH_VARARGS, "Widget_ungrabGesture."},
    {"Widget_widgetByIndex", Widget_widgetByIndex, METH_VARARGS, "Widget_widgetByIndex."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "HostLib",
    NULL,
    -1,
    Methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyObject* InitializeHostLibModule()
{
    return PythonSupport::instance()->createAndAddModule(&moduledef);
}
#endif // PY_MAJOR_VERSION >= 3
#endif // !USE_THRIFT

bool Application::initialize()
{
    m_python_home = PythonSupport::ensurePython(m_python_home);
#if !defined(DEBUG) && !defined(Q_OS_LINUX)
    if (m_python_home.isEmpty() || !QFile(m_python_home).exists())
        return false;
#endif

    m_idle_enabled = true;

    PythonSupport::initInstance(m_python_home);
#if !USE_THRIFT
#if PY_MAJOR_VERSION >= 3
    PythonSupport::instance()->initializeModule("HostLib", &InitializeHostLibModule);
#endif // PY_MAJOR_VERSION >= 3

    PythonSupport::instance()->initialize(m_python_home);  // initialize Python support

    Python_ThreadBlock thread_block;

#if PY_MAJOR_VERSION < 3
    PyObject* host_lib_mod = Py_InitModule("HostLib", Methods); //borrowed reference
#endif // PY_MAJOR_VERSION < 3

    // Add the resources path so that the Python imports work. This is necessary to find bootstrap.py,
    // which may not be in the same directory as the executable (specifically for Mac OS where things
    // are arranged into a bundle).
    if (m_python_target.isEmpty())
        m_python_target = resourcesPath();
    PythonSupport::instance()->addResourcePath(m_python_target);

#if PY_MAJOR_VERSION < 3
    PythonSupport::instance()->addPyObjectToModuleFromQVariant(host_lib_mod, "original_resources_path", QVariant(resourcesPath()));

    // Add 'arguments' to the HostLib module. We'll convert to a QVariant and use our
	// automatic conversion routines.
	// Now both idc and arguments can be obtained by simply
	// import HostLib; print HostLib.arguments, HostLib.idc
	QVariant qv(this->arguments());
	PythonSupport::instance()->addPyObjectToModuleFromQVariant(host_lib_mod, "arguments", qv);
#endif // PY_MAJOR_VERSION < 3

    // Bootstrap the python stuff.
    PyObject *module = PythonSupport::instance()->import("bootstrap");
    PythonSupport::instance()->printAndClearErrors();
	m_bootstrap_module = PyObjectToQVariant(module); // new reference
    PythonSupport::instance()->printAndClearErrors();
    m_py_application = invokePyMethod(m_bootstrap_module, "bootstrap_main", QVariantList() << arguments() << resourcesPath());

    return invokePyMethod(m_py_application, "start", QVariantList()).toBool();
#else // !USE_THRIFT
    boost::shared_ptr<TSocket> socket(new TSocket("localhost", 9091));
    boost::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
    boost::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    transport->open();
    callbacks = new GUICallbacksClient(protocol);
    return true;
#endif // !USE_THRIFT
}

void Application::idle()
{
    if (!m_idle_enabled)
        return;
    
#if !USE_THRIFT
    try
    {
        invokePyMethod(m_bootstrap_module, "bootstrap_periodic", QVariantList());
    }
    catch (...)
    {
        LOG_EXCEPTION("Application::idle");
    }
#endif // !USE_THRIFT
}

void Application::continueQuit()
{
    if (m_quit_on_last_window)
        quit();
}

void Application::aboutToQuit()
{
    invokePyMethod(m_py_application, "stop", QVariantList());
}

QString Application::resourcesPath() const
{
#if defined(Q_OS_MAC)
    return qApp->applicationDirPath()+"/../Resources/";
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return qApp->applicationDirPath()+"/";
#endif
}

#if !USE_THRIFT
QVariant Application::lookupPyObjectByName(const QString &object)
{
    return PythonSupport::instance()->lookupPyObjectByName(object);
}

QVariant Application::invokePyMethod(const QVariant &object, const QString &method, const QVariantList &args)
{
    return PythonSupport::instance()->invokePyMethod(object, method, args);
}

bool Application::setPyObjectAttribute(const QVariant &object, const QString &attribute, const QVariant &value)
{
    return PythonSupport::instance()->setAttribute(object, attribute, value);
}

QVariant Application::getPyObjectAttribute(const QVariant &object, const QString &attribute)
{
    return PythonSupport::instance()->getAttribute(object, attribute);
}
#endif // !USE_THRIFT

QVariant Application::dispatchPyMethod(const QVariant &object, const QString &method, const QVariantList &args)
{
#if USE_THRIFT
    if (method != "periodic")
        qDebug() << "dispatch " << object << "," << method;
    return 0;
#else // USE_THRIFT
    return invokePyMethod(m_bootstrap_module, "bootstrap_dispatch", QVariantList() << object << method << QVariant(args));
#endif // USE_THRIFT
}

#if USE_THRIFT

Q_DECLARE_METATYPE(std::string)

void GUIDrawingCommandsToCanvasDrawingCommands(const GUIDrawingCommands& gui_drawing_commands, QList<CanvasDrawingCommand> &drawing_commands)
{
    Q_FOREACH(const GUIDrawingCommand &gui_drawing_command, gui_drawing_commands)
    {
        CanvasDrawingCommand drawing_command;
        drawing_command.command = QString::fromStdString(gui_drawing_command.command);
        QVariantList arguments;
        Q_FOREACH(const GUIDrawingCommandArg &gui_drawing_command_arg, gui_drawing_command.arguments)
        {
            switch (gui_drawing_command_arg.type)
            {
                case GUIVariantType::DOUBLE:
                    arguments.append(gui_drawing_command_arg.variant.v_double);
                    break;
                case GUIVariantType::I32:
                    arguments.append(gui_drawing_command_arg.variant.v_i32);
                    break;
                case GUIVariantType::I64:
                    arguments.append(gui_drawing_command_arg.variant.v_i64);
                    break;
                case GUIVariantType::BOOL:
                    arguments.append(gui_drawing_command_arg.variant.v_bool);
                    break;
                case GUIVariantType::STRING:
                    arguments.append(QString::fromStdString(gui_drawing_command_arg.variant.v_string));
                    break;
                case GUIVariantType::BINARY:
                    arguments.append(QVariant::fromValue<std::string>(gui_drawing_command_arg.variant.v_binary));
                    break;
            }
        }
        drawing_command.arguments = arguments;
        drawing_commands.append(drawing_command);
    }
}
#endif