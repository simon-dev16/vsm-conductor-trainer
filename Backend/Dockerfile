FROM python:3.12-slim
WORKDIR /app
COPY Backend/requirements.txt /app/Backend/requirements.txt
RUN pip install --no-cache-dir -r Backend/requirements.txt && useradd --create-home --uid 10001 vsm
COPY Backend /app/Backend
COPY Contracts /app/Contracts
RUN mkdir -p /app/Backend/data && chown -R vsm:vsm /app
USER vsm
WORKDIR /app/Backend
EXPOSE 18767
CMD ["uvicorn", "asgi:app", "--host", "0.0.0.0", "--port", "18767", "--no-access-log"]
