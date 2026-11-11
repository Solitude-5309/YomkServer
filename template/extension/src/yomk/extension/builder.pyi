from yomk.msg.server import Response


class ExtensionBuilder:
    def version(self) -> Response: ...


def extension() -> ExtensionBuilder: ...
