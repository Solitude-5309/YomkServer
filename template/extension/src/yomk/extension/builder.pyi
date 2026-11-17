from yomk.msgs.server import Response


class ExtensionBuilder:
    def version(self) -> Response: ...


def extension() -> ExtensionBuilder: ...
