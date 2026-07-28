"""Final Host build subprocess-orchestration tests."""

from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import host_build_adapter as adapter
from tools import host_build_request
from tools import host_cmake_target
from tools.tests import test_host_build_request as request_tests


def _query(build_root: Path) -> host_cmake_target.HostCMakeFileApiQueryEvidence:
    return host_cmake_target.HostCMakeFileApiQueryEvidence(
        build_root=build_root,
        query_path=build_root / ".cmake/api/v1/query/client/query.json",
        client_name=host_cmake_target.HOST_BUILD_FILE_API_CLIENT,
        codemodel_major=2,
        codemodel_minor=6,
    )


def _target(
    build_root: Path,
    target_name: str,
) -> host_cmake_target.HostCMakeTargetEvidence:
    relative = f"asharia-host/bin/Debug/{target_name}.exe"
    return host_cmake_target.HostCMakeTargetEvidence(
        build_root=build_root,
        reply_index_path=build_root / ".cmake/api/v1/reply/index.json",
        configuration="Debug",
        target_name=target_name,
        target_type="EXECUTABLE",
        name_on_disk=f"{target_name}.exe",
        artifact_relative_path=relative,
        artifact_path=build_root / relative,
        codemodel_major=2,
        codemodel_minor=6,
    )


class FinalHostBuildAdapterTests(unittest.TestCase):
    def test_adapter_reexports_the_public_request_type(self) -> None:
        self.assertIs(
            host_build_request.FinalHostBuildRequestV1,
            adapter.FinalHostBuildRequestV1,
        )

    def test_execute_uses_one_argument_vector_without_shell_parsing(self) -> None:
        arguments = ("cmake", "--build", "build", "--target", "host")
        source_root = Path("source")
        environment = {"PATH": "tools"}
        completed = adapter.subprocess.CompletedProcess(arguments, 0, b"", b"")

        with mock.patch.object(
            adapter.subprocess, "run", return_value=completed
        ) as run_mock:
            evidence, diagnostic = adapter._execute(
                "build", arguments, source_root, environment, 30.0
            )

        self.assertIsNone(diagnostic)
        self.assertEqual(
            adapter.HostBuildProcessEvidence("build", arguments, 0),
            evidence,
        )
        run_mock.assert_called_once_with(
            arguments,
            cwd=source_root,
            env=environment,
            stdin=adapter.subprocess.DEVNULL,
            stdout=adapter.subprocess.PIPE,
            stderr=adapter.subprocess.PIPE,
            timeout=30.0,
            check=False,
            shell=False,
        )

    def test_success_runs_configure_then_two_phase_target_reads_then_build(self) -> None:
        with request_tests.build_request_fixture() as (request, validators):
            validated, diagnostics = request_tests.validate(request, validators)
            self.assertEqual([], diagnostics)
            assert validated is not None
            query = _query(validated.build_root)
            target = _target(query.build_root, request.target_name)
            configure_arguments = ("cmake", "configure")
            build_arguments = ("cmake", "build")
            configure = adapter.HostBuildProcessEvidence(
                "configure", configure_arguments, 0
            )
            build = adapter.HostBuildProcessEvidence("build", build_arguments, 0)

            with (
                mock.patch.object(
                    host_build_request,
                    "validate_final_host_build_request",
                    return_value=(validated, []),
                ),
                mock.patch.object(
                    host_cmake_target,
                    "write_host_cmake_file_api_query",
                    return_value=host_cmake_target.HostCMakeFileApiQueryResult(
                        query, ()
                    ),
                ),
                mock.patch.object(
                    host_build_request,
                    "configure_final_host_arguments",
                    return_value=configure_arguments,
                ) as configure_arguments_mock,
                mock.patch.object(
                    host_build_request,
                    "build_final_host_arguments",
                    return_value=build_arguments,
                ) as build_arguments_mock,
                mock.patch.object(
                    adapter,
                    "_execute",
                    side_effect=((configure, None), (build, None)),
                ) as execute_mock,
                mock.patch.object(
                    host_cmake_target,
                    "read_host_cmake_target",
                    side_effect=(
                        host_cmake_target.HostCMakeTargetResult(target, ()),
                        host_cmake_target.HostCMakeTargetResult(target, ()),
                    ),
                ) as read_mock,
            ):
                result = adapter.run_final_host_build(request, validators)

            self.assertTrue(result.succeeded)
            self.assertEqual(target, result.target)
            self.assertEqual(
                ["configure", "build"],
                [call.args[0] for call in execute_mock.call_args_list],
            )
            configure_arguments_mock.assert_called_once_with(
                request, validated, query.build_root
            )
            build_arguments_mock.assert_called_once_with(
                request, validated, query.build_root
            )
            self.assertEqual(False, read_mock.call_args_list[0].kwargs["require_artifact"])
            self.assertEqual(True, read_mock.call_args_list[1].kwargs["require_artifact"])

    def test_configure_failure_stops_before_target_read_and_build(self) -> None:
        with request_tests.build_request_fixture() as (request, validators):
            validated, diagnostics = request_tests.validate(request, validators)
            self.assertEqual([], diagnostics)
            assert validated is not None
            query = _query(validated.build_root)
            configure = adapter.HostBuildProcessEvidence(
                "configure", ("cmake", "configure"), 1
            )
            failure = contracts.Diagnostic(
                "host-build.configure-failed",
                "final-host-build",
                "/configure",
                "configure process returned exit code 1",
            )

            with (
                mock.patch.object(
                    host_build_request,
                    "validate_final_host_build_request",
                    return_value=(validated, []),
                ),
                mock.patch.object(
                    host_cmake_target,
                    "write_host_cmake_file_api_query",
                    return_value=host_cmake_target.HostCMakeFileApiQueryResult(
                        query, ()
                    ),
                ),
                mock.patch.object(
                    host_build_request,
                    "configure_final_host_arguments",
                    return_value=configure.arguments,
                ),
                mock.patch.object(
                    adapter,
                    "_execute",
                    return_value=(configure, failure),
                ),
                mock.patch.object(
                    host_cmake_target, "read_host_cmake_target"
                ) as read_mock,
                mock.patch.object(
                    host_build_request, "build_final_host_arguments"
                ) as build_arguments_mock,
            ):
                result = adapter.run_final_host_build(request, validators)

            self.assertFalse(result.succeeded)
            self.assertIsNone(result.target)
            self.assertEqual(configure, result.configure)
            self.assertIsNone(result.build)
            read_mock.assert_not_called()
            build_arguments_mock.assert_not_called()

    def test_request_failure_stops_before_file_api_or_process_execution(self) -> None:
        diagnostic = contracts.Diagnostic(
            "host-build.request-invalid",
            "final-host-build",
            "",
            "build request must use FinalHostBuildRequestV1",
        )
        with (
            mock.patch.object(
                host_build_request,
                "validate_final_host_build_request",
                return_value=(None, [diagnostic]),
            ),
            mock.patch.object(
                host_cmake_target, "write_host_cmake_file_api_query"
            ) as query_mock,
            mock.patch.object(adapter, "_execute") as execute_mock,
        ):
            result = adapter.run_final_host_build(object(), mock.sentinel.validators)

        self.assertFalse(result.succeeded)
        self.assertEqual((diagnostic,), result.diagnostics)
        query_mock.assert_not_called()
        execute_mock.assert_not_called()


if __name__ == "__main__":
    unittest.main()
