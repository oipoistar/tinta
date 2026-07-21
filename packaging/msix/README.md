# MSIX packaging & Microsoft Store publishing

Tinta ships on the Microsoft Store as **Tinta Markdown Viewer**
([`9MZ5MZ3L9RKF`](https://apps.microsoft.com/detail/9MZ5MZ3L9RKF)).

## Build the package locally

```pwsh
pwsh packaging/msix/pack.ps1 -ExePath build/Release/tinta.exe
```

Stamps the version from `CMakeLists.txt` (`x.y.z` → `x.y.z.0`), builds
`resources.pri`, and packs an **unsigned** MSIX — the Store signs submitted
packages, so no certificate is needed.

## Automatic Store publishing (CI)

The `store` job in `.github/workflows/build.yml` runs on every `v*` tag after
the GitHub release is created. It repacks the exact released `tinta.exe` into
an MSIX and submits it with [msstore-cli](https://github.com/microsoft/msstore-cli).
The Store then certifies and publishes automatically; Store installs
auto-update on users' machines.

The job **skips itself** unless these repository secrets are set:

| Secret | Value |
|---|---|
| `MSSTORE_TENANT_ID` | Entra (Azure AD) tenant ID associated with the Partner Center account |
| `MSSTORE_SELLER_ID` | Partner Center seller ID (Account settings → Organization profile) |
| `MSSTORE_CLIENT_ID` | App registration (client) ID |
| `MSSTORE_CLIENT_SECRET` | Client secret for that app registration |

One-time setup: associate an Entra tenant with Partner Center
(Account settings → Tenants), create an Azure AD application under
Account settings → User management → Azure AD applications with the
**Manager (Windows)** role, create a client secret for it, then:

```pwsh
gh secret set MSSTORE_TENANT_ID
gh secret set MSSTORE_SELLER_ID
gh secret set MSSTORE_CLIENT_ID
gh secret set MSSTORE_CLIENT_SECRET
```

Running `msstore reconfigure` locally walks through the same setup
interactively and can create the Entra application for you.

Note: the Store requires each submission's package version to be greater than
the last published one, so the job only succeeds for new version tags.

## Files

- `AppxManifest.xml` — package identity, full-trust entry point, declared
  `.md`/`.markdown`/`.mmd` file-type associations. `TINTA_MSIX_VERSION` is
  replaced by `pack.ps1`.
- `Assets/` — Store logos generated from `resources/tinta.ico`.
- Store listing art (marketing screenshots, poster/box art) is managed in
  Partner Center; sources live outside this repo.
