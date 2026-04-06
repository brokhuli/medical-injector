#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "grpc/GrpcClientService.h"
#include "bridge/InjectorBridge.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Medical Injector Simulator");
    app.setOrganizationName("MedicalInjector");

    // Use Basic style for cross-platform consistency
    QQuickStyle::setStyle("Basic");

    // Parse backend address from command line (--backend=host:port)
    QString backendAddress = "localhost:50051";
    for (int i = 1; i < argc; ++i) {
        QString arg(argv[i]);
        if (arg.startsWith("--backend=")) {
            backendAddress = arg.mid(10);
        }
    }

    // Create gRPC client and bridge
    injector::frontend::GrpcClientService grpcClient(backendAddress.toStdString());
    injector::frontend::InjectorBridge bridge(&grpcClient);

    // Set up QML engine
    QQmlApplicationEngine engine;

    // Expose bridge to QML
    engine.rootContext()->setContextProperty("injectorBridge", &bridge);

    // Load QML
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // Start gRPC connection (after QML is loaded so UI shows "connecting" state)
    grpcClient.connect();

    int result = app.exec();

    // Clean shutdown
    grpcClient.disconnect();
    return result;
}
