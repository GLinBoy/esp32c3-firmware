# Contributing to ESP32C3 Firmware

## Development Workflow

1. **Clone repository**:

   ```bash
   git clone git@github.com:GLinBoy/esp32c3-firmware.git
   cd esp32c3-firmware
   ```

2. **Create feature branch**:

   ```bash
   git checkout -b feature/my-feature
   ```

3. **Make changes, test locally**:

   ```bash
   pio run --target upload
   pio device monitor
   ```

4. **Commit with conventional commits**:

   ```bash
   git commit -m "feat: add new feature description"
   ```

5. **Push and create PR**:

   ```bash
   git push -u origin feature/my-feature
   ```

   Open PR on GitHub, CI will run build verification.

6. **After merge, delete feature branch**:

   ```bash
   git branch -d feature/my-feature
   ```

## Releasing

Only maintainers create releases:

```bash
git checkout main
git pull
git tag v1.x.x
git push origin v1.x.x
```

GitHub Actions automatically builds and publishes release binaries.

## Partition Table Changes

If you modify `partitions.csv`:

```bash
rm -rf .pio/build
rm sdkconfig*
pio run --target fullclean
pio run
```

Incremental builds silently fail to pick up partition changes.
