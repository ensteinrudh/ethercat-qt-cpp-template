#include "ethercatcontroller.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <csignal>

// Global controller pointer for signal handler
static EtherCatController *g_controller = nullptr;

// Signal handler
void signalHandler(int sig) {
  qDebug() << "Signal" << sig << "received, shutting down";
  if (g_controller) {
    g_controller->shutdown();
  }
  QCoreApplication::quit();
}

int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);

  // Create the EtherCAT controller
  EtherCatController ethercatController;
  g_controller = &ethercatController;

  // Set up signal handlers for clean shutdown
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // Initialize QML engine
  QQmlApplicationEngine engine;

  // Make the controller available to QML
  engine.rootContext()->setContextProperty("ethercatController",
                                           &ethercatController);

  // Connect to error signals
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  // Load QML
  engine.loadFromModule("qmletherui", "Main");

  // Initialize EtherCAT after UI is shown
  QMetaObject::invokeMethod(&ethercatController, "initialize",
                            Qt::QueuedConnection);

  // Connect app about to quit signal to ensure clean shutdown
  QObject::connect(&app, &QGuiApplication::aboutToQuit,
                   [&ethercatController]() { ethercatController.shutdown(); });

  return app.exec();
}
