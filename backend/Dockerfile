FROM golang:1.21-alpine as development

WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN go install github.com/cosmtrek/air@latest

CMD ["air"]

FROM golang:1.21-alpine as builder
WORKDIR /app
COPY . .
RUN go build -o main ./cmd/server

FROM alpine:latest as production
RUN apk --no-cache add ca-certificates
WORKDIR /root/
COPY --from=builder /app/main .
EXPOSE 8080
CMD ["./main"]