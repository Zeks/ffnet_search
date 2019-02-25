import qbs
Module {
	Depends { name: "cpp" }
    property string debugAppend: ""
    property bool logger : true
    property bool grpc: true
    property bool usePrecompiledHeader: true

    property string projectPath: qbs.toolchain.contains("msvc") ? "F:/Programming/Flipper" : "/home/zekses/flipper"
    property string protoc: qbs.toolchain.contains("msvc") ? "E:/Programming/protobuf/protoc.exe":"/home/zekses/Downloads/grpc/bins/opt/protobuf/protoc"
    property string grpcPlugin: qbs.toolchain.contains("msvc") ?"E:/Programming/protobuf/grpc_cpp_plugin.exe": "/home/zekses/Downloads/grpc/bins/opt/grpc_cpp_plugin"

    property string protobufName: "libprotobufd"
    property stringList zlib : qbs.toolchain.contains("msvc") ? ["zlib", "cares", "crypto"] : []
    property stringList ssl : qbs.toolchain.contains("msvc") ? ["ssl"] : []

}
