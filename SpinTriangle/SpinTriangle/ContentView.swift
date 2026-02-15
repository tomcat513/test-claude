import SwiftUI

struct Triangle: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.midX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        path.closeSubpath()
        return path
    }
}

struct ContentView: View {
    @State private var rotation: Double = 0

    var body: some View {
        VStack(spacing: 60) {
            Spacer()

            Triangle()
                .fill(
                    LinearGradient(
                        colors: [.blue, .purple, .pink],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
                .frame(width: 200, height: 200)
                .rotationEffect(.degrees(rotation))
                .shadow(color: .purple.opacity(0.4), radius: 10, y: 5)

            Spacer()

            Button {
                withAnimation(.easeInOut(duration: 1.0)) {
                    rotation += 360
                }
            } label: {
                Text("Spin")
                    .font(.title2.bold())
                    .foregroundColor(.white)
                    .frame(width: 160, height: 50)
                    .background(
                        LinearGradient(
                            colors: [.blue, .purple],
                            startPoint: .leading,
                            endPoint: .trailing
                        )
                    )
                    .cornerRadius(25)
            }

            Spacer()
        }
        .padding()
    }
}

#Preview {
    ContentView()
}
